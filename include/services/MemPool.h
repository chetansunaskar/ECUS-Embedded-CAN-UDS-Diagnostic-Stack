/**
 * @file    MemPool.h
 * @brief   Fixed-size block memory pool — deterministic O(1) allocation.
 *
 * Why a memory pool instead of malloc() in embedded systems:
 *  - malloc() is non-deterministic (unpredictable latency, fragmentation).
 *  - MISRA-C:2012 Rule 21.3 bans dynamic allocation in safety-critical code.
 *  - A fixed-size pool guarantees O(1) alloc/free and zero fragmentation.
 *
 * Design:
 *  - Pool is backed by a caller-supplied static buffer (zero heap usage).
 *  - Free list is a singly-linked list of FreeNode structs overlaid on the
 *    free blocks themselves (no extra bookkeeping memory).
 *  - Thread-safe via an optional POSIX mutex (compile with ECUS_POOL_THREAD_SAFE).
 *  - Supports multiple independent pool instances via the MemPool struct.
 *  - Canary value at each block's header detects buffer overflows.
 *
 * C concepts demonstrated:
 *   Dynamic memory concepts (pool pattern), void pointers, pointer casting,
 *   bit-fields (pool stats), struct embedding, alignment macros,
 *   compile-time size assertions.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef MEM_POOL_H
#define MEM_POOL_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"

#ifdef ECUS_POOL_THREAD_SAFE
#  include <pthread.h>
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */

/** Magic canary value stored at each block header to detect corruption. */
#define MEM_POOL_CANARY   0xDEADC0DEU

/* =========================================================================
 * Pool instance structure
 *
 * Callers allocate this struct (statically).  The backing buffer must have
 * lifetime >= the pool instance.
 * ========================================================================= */
typedef struct MemPool
{
    /* --- Configuration (set by Init, read-only thereafter) --- */
    void       *buffer;         /**< Pointer to the backing memory buffer. */
    size_t      blockSize;      /**< Size of each block (bytes, aligned).  */
    size_t      blockCount;     /**< Total number of blocks in the pool.   */

    /* --- Runtime state --- */
    void       *freeListHead;   /**< Head of the free-block linked list.   */

    /* --- Statistics (bit-fields to pack into minimal memory) --- */
    struct {
        uint32_t  totalAllocs  : 20; /**< Cumulative allocations.          */
        uint32_t  totalFrees   : 20; /**< Cumulative frees.                */
        uint32_t  peakUsed     : 12; /**< Peak simultaneous blocks in use. */
        uint32_t  currentUsed  : 12; /**< Currently allocated blocks.      */
    } stats;  /* Note: bit-fields are illustrative; use plain fields for MISRA */

#ifdef ECUS_POOL_THREAD_SAFE
    pthread_mutex_t mutex;
#endif

    bool        initialised;    /**< Guard against double-init.            */
} MemPool;

/* =========================================================================
 * Helper macro: declare a pool + its backing buffer as static locals.
 *
 * Usage:
 *   MEMPOOL_DECLARE_STATIC(g_canPool, 64, 32);
 *   // declares: static uint8_t g_canPool_buf[64 * 32]; (aligned)
 *   //           static MemPool g_canPool;
 * ========================================================================= */
#define MEMPOOL_DECLARE_STATIC(name, blockSz, count)                     \
    static uint8_t ECUS_ATTR_ALIGNED(8)                                   \
        name##_buf[(size_t)(blockSz) * (size_t)(count)];                 \
    static MemPool name

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise a memory pool.
 *
 * @param  pool        Pointer to caller-allocated MemPool instance.
 * @param  buffer      Backing memory buffer (must be 8-byte aligned).
 * @param  blockSize   Size of each individual block in bytes (>= sizeof(void*)).
 * @param  blockCount  Number of blocks in the pool.
 * @return ECUS_OK on success.
 */
EcusStatus MemPool_Init(MemPool *pool,
                         void    *buffer,
                         size_t   blockSize,
                         size_t   blockCount);

/**
 * @brief  Allocate one block from the pool.
 *
 * @param  pool  Initialised pool instance.
 * @return Pointer to a zeroed block, or NULL if the pool is exhausted.
 */
ECUS_ATTR_WARN_UNUSED
void *MemPool_Alloc(MemPool *pool);

/**
 * @brief  Return a block to the pool.
 *
 * @param  pool   Pool the block was allocated from.
 * @param  block  Pointer previously returned by MemPool_Alloc().
 * @return ECUS_OK on success, ECUS_ERR_INVALID_PARAM if block is out of range.
 */
EcusStatus MemPool_Free(MemPool *pool, void *block);

/**
 * @brief  Return the number of free blocks remaining.
 * @param  pool  Initialised pool instance.
 * @return Count of free blocks.
 */
size_t MemPool_FreeCount(const MemPool *pool);

/**
 * @brief  Print pool statistics to the logger.
 * @param  pool   Initialised pool instance.
 * @param  label  Optional label string printed in the header.
 */
void MemPool_PrintStats(const MemPool *pool, const char *label);

/**
 * @brief  Reset pool to fully-free state (all blocks reclaimed).
 *         Warning: any outstanding pointers become invalid after this call.
 * @param  pool  Initialised pool instance.
 */
void MemPool_Reset(MemPool *pool);

#endif /* MEM_POOL_H */
