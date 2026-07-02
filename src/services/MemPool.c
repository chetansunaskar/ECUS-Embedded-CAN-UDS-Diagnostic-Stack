/**
 * @file    MemPool.c
 * @brief   Fixed-size block memory pool implementation.
 *
 * C concepts demonstrated:
 *   void* / pointer casting, pointer arithmetic (uint8_t* walking),
 *   struct overlay (FreeNode on raw buffer bytes),
 *   alignment verification (ECUS_ALIGN_UP macro),
 *   boundary check (block-in-range validation),
 *   memset for zero-initialisation of allocated blocks,
 *   optional POSIX mutex via conditional compilation.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "services/MemPool.h"
#include "services/Logger.h"

#include <string.h>    /* memset */

/* =========================================================================
 * Private: free-list node (overlaid on free blocks — zero extra memory)
 * ========================================================================= */

/** A free-list node is just a single "next" pointer stored in the block. */
typedef struct FreeNode
{
    struct FreeNode *next;
} FreeNode;

/* Compile-time guard: blocks must be large enough to hold a pointer. */
STATIC_ASSERT(sizeof(FreeNode) <= 8U,
              "FreeNode must fit in a minimum-size pool block");

/* =========================================================================
 * Private helpers
 * ========================================================================= */

#ifdef ECUS_POOL_THREAD_SAFE
#  define POOL_LOCK(p)    (void)pthread_mutex_lock(&(p)->mutex)
#  define POOL_UNLOCK(p)  (void)pthread_mutex_unlock(&(p)->mutex)
#else
#  define POOL_LOCK(p)    do {} while (0)
#  define POOL_UNLOCK(p)  do {} while (0)
#endif

/**
 * @brief  Check whether @p block lies within the pool's buffer.
 *         Demonstrates pointer arithmetic and range validation.
 */
static bool IsBlockInRange(const MemPool *pool, const void *block)
{
    /* Cast to uint8_t* for byte-level pointer arithmetic (defined behaviour). */
    const uint8_t *base = (const uint8_t *)pool->buffer;
    const uint8_t *end  = base + (pool->blockSize * pool->blockCount);
    const uint8_t *blk  = (const uint8_t *)block;

    return (blk >= base) && (blk < end);
}

/* =========================================================================
 * Public API implementation
 * ========================================================================= */

EcusStatus MemPool_Init(MemPool *pool,
                         void    *buffer,
                         size_t   blockSize,
                         size_t   blockCount)
{
    ECUS_CHECK(pool       != NULL, ECUS_ERR_NULL_PTR,     return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(buffer     != NULL, ECUS_ERR_NULL_PTR,     return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(blockSize  >= sizeof(FreeNode),
               ECUS_ERR_INVALID_PARAM, return ECUS_ERR_INVALID_PARAM);
    ECUS_CHECK(blockCount >  0U,   ECUS_ERR_INVALID_PARAM,return ECUS_ERR_INVALID_PARAM);
    ECUS_CHECK(!pool->initialised, ECUS_ERR_ALREADY_INIT, return ECUS_ERR_ALREADY_INIT);

    /* Enforce 8-byte alignment on blockSize (required for safe pointer casts). */
    blockSize = (size_t)ECUS_ALIGN_UP(blockSize, 8U);

    pool->buffer     = buffer;
    pool->blockSize  = blockSize;
    pool->blockCount = blockCount;

    /* Zero the stats struct. */
    (void)memset(&pool->stats, 0, sizeof(pool->stats));

#ifdef ECUS_POOL_THREAD_SAFE
    if (pthread_mutex_init(&pool->mutex, NULL) != 0)
    {
        return ECUS_ERR_NO_MEMORY;
    }
#endif

    /* Build the free list by walking the buffer in blockSize steps.
     * Each free block's first sizeof(FreeNode) bytes become a FreeNode.
     * Pointer arithmetic: current = buffer + i * blockSize.
     */
    uint8_t *cur  = (uint8_t *)buffer;

    for (size_t i = 0U; i < blockCount; i++)
    {
        FreeNode *node = (FreeNode *)(void *)cur;   /* cast: defined per C11 6.3.2.3 */

        if (i < (blockCount - 1U))
        {
            /* Point to the next block in the buffer. */
            node->next = (FreeNode *)(void *)(cur + blockSize);
        }
        else
        {
            /* Last block: terminate the list. */
            node->next = NULL;
        }

        cur += blockSize;   /* pointer arithmetic */
    }

    pool->freeListHead  = buffer;
    pool->initialised   = true;

    LOG_DEBUG("MemPool init: blockSize=%zu  count=%zu  total=%zu bytes",
              blockSize, blockCount, blockSize * blockCount);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void *MemPool_Alloc(MemPool *pool)
{
    void *block = NULL;

    if ((pool == NULL) || !pool->initialised)
    {
        return NULL;
    }

    POOL_LOCK(pool);

    if (pool->freeListHead != NULL)
    {
        /* Pop the head of the free list. */
        FreeNode *node      = (FreeNode *)pool->freeListHead;
        pool->freeListHead  = node->next;

        block = (void *)node;

        /* Zero-initialise the block before handing it to the caller.
         * This prevents information leakage between allocations. */
        (void)memset(block, 0, pool->blockSize);

        pool->stats.totalAllocs++;
        pool->stats.currentUsed++;

        if (pool->stats.currentUsed > pool->stats.peakUsed)
        {
            pool->stats.peakUsed = pool->stats.currentUsed;
        }
    }
    else
    {
        LOG_WARN("MemPool exhausted (blockSize=%zu, count=%zu)",
                 pool->blockSize, pool->blockCount);
        ErrorHandler_SetLastError(ECUS_ERR_POOL_EXHAUSTED);
    }

    POOL_UNLOCK(pool);

    return block;
}

/* -------------------------------------------------------------------------- */

EcusStatus MemPool_Free(MemPool *pool, void *block)
{
    ECUS_CHECK(pool  != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(block != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(pool->initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    /* Validate that the pointer actually belongs to this pool. */
    if (!IsBlockInRange(pool, block))
    {
        LOG_ERROR("MemPool_Free: block %p not in pool range!", block);
        return ECUS_ERR_INVALID_PARAM;
    }

    POOL_LOCK(pool);

    /* Poison the block content (0xFE pattern) to catch use-after-free. */
    (void)memset(block, 0xFE, pool->blockSize);

    /* Push block onto the free list head (O(1)). */
    FreeNode *node     = (FreeNode *)block;
    node->next         = (FreeNode *)pool->freeListHead;
    pool->freeListHead = block;

    if (pool->stats.currentUsed > 0U)
    {
        pool->stats.currentUsed--;
    }
    pool->stats.totalFrees++;

    POOL_UNLOCK(pool);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

size_t MemPool_FreeCount(const MemPool *pool)
{
    size_t count = 0U;

    if ((pool == NULL) || !pool->initialised)
    {
        return 0U;
    }

    /* Walk the free list (O(n) — acceptable for diagnostics). */
    const FreeNode *node = (const FreeNode *)pool->freeListHead;
    while (node != NULL)
    {
        count++;
        node = node->next;
    }

    return count;
}

/* -------------------------------------------------------------------------- */

void MemPool_PrintStats(const MemPool *pool, const char *label)
{
    if ((pool == NULL) || !pool->initialised)
    {
        return;
    }

    size_t freeCount = MemPool_FreeCount(pool);

    LOG_INFO("MemPool [%s]: blockSize=%zu  total=%zu  "
             "free=%zu  used=%u  peak=%u  allocs=%u  frees=%u",
             (label != NULL) ? label : "?",
             pool->blockSize,
             pool->blockCount,
             freeCount,
             pool->stats.currentUsed,
             pool->stats.peakUsed,
             pool->stats.totalAllocs,
             pool->stats.totalFrees);
}

/* -------------------------------------------------------------------------- */

void MemPool_Reset(MemPool *pool)
{
    if ((pool == NULL) || !pool->initialised)
    {
        return;
    }

    POOL_LOCK(pool);

    /* Rebuild the entire free list from scratch. */
    uint8_t *cur = (uint8_t *)pool->buffer;

    for (size_t i = 0U; i < pool->blockCount; i++)
    {
        FreeNode *node = (FreeNode *)(void *)cur;
        node->next     = (i < (pool->blockCount - 1U))
                         ? (FreeNode *)(void *)(cur + pool->blockSize)
                         : NULL;
        cur += pool->blockSize;
    }

    pool->freeListHead          = pool->buffer;
    pool->stats.currentUsed     = 0U;

    POOL_UNLOCK(pool);

    LOG_DEBUG("MemPool reset (all %zu blocks reclaimed)", pool->blockCount);
}
