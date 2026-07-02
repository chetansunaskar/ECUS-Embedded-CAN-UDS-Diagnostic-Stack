/**
 * @file    RingBuffer.c
 * @brief   Lock-free SPSC ring buffer implementation.
 *
 * C concepts demonstrated:
 *   C11 atomics with explicit memory ordering,
 *   memcpy for generic element copy,
 *   power-of-2 bitmask index wrapping,
 *   uint8_t* pointer arithmetic for flat storage indexing.
 *
 * Memory ordering rationale (SPSC, no mutex):
 *   Push (producer):
 *     - Load tail with memory_order_acquire  (see latest consumer advance)
 *     - Store head with memory_order_release (publish new element to consumer)
 *   Pop (consumer):
 *     - Load head with memory_order_acquire  (see latest producer advance)
 *     - Store tail with memory_order_release (publish slot reclaim to producer)
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "transport/RingBuffer.h"
#include "services/Logger.h"

#include <string.h>   /* memcpy */

/* =========================================================================
 * Private: check if n is a power of 2
 * ========================================================================= */
static bool IsPowerOfTwo(size_t n)
{
    return (n > 0U) && ((n & (n - 1U)) == 0U);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus RingBuffer_Init(RingBuffer *rb,
                            void       *storage,
                            size_t      elemSize,
                            size_t      capacity)
{
    ECUS_CHECK(rb       != NULL, ECUS_ERR_NULL_PTR,      return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(storage  != NULL, ECUS_ERR_NULL_PTR,      return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(elemSize >  0U,   ECUS_ERR_INVALID_PARAM, return ECUS_ERR_INVALID_PARAM);
    ECUS_CHECK(IsPowerOfTwo(capacity), ECUS_ERR_INVALID_PARAM,
               return ECUS_ERR_INVALID_PARAM);

    rb->storage     = (uint8_t *)storage;
    rb->elemSize    = elemSize;
    rb->capacity    = capacity;
    rb->mask        = capacity - 1U;    /* e.g. cap=16 → mask=0x0F */

    atomic_init(&rb->head, 0U);
    atomic_init(&rb->tail, 0U);

    rb->initialised = true;

    LOG_DEBUG("RingBuffer init: elemSize=%zu  capacity=%zu  storage=%p",
              elemSize, capacity, storage);
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus RingBuffer_Push(RingBuffer *rb, const void *elem)
{
    ECUS_CHECK(rb   != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(elem != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(rb->initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    /* Read current head (producer's own counter — relaxed is fine here,
     * but acquire pairs with consumer's release on tail update).           */
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if ((head - tail) >= rb->capacity)
    {
        /* Buffer full. */
        return ECUS_ERR_OVERFLOW;
    }

    /* Compute slot address using pointer arithmetic + bitmask wrapping.
     * slot_index = head & mask   (avoids modulo — O(1), branchless)      */
    size_t   slotIndex = head & rb->mask;
    uint8_t *slotPtr   = rb->storage + (slotIndex * rb->elemSize);

    /* Copy element into slot (generic: works for any POD type). */
    (void)memcpy(slotPtr, elem, rb->elemSize);

    /* Publish the new head to the consumer side. */
    atomic_store_explicit(&rb->head, head + 1U, memory_order_release);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus RingBuffer_Pop(RingBuffer *rb, void *elem)
{
    ECUS_CHECK(rb   != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(elem != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(rb->initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (tail == head)
    {
        /* Buffer empty. */
        return ECUS_ERR_UNDERFLOW;
    }

    size_t   slotIndex = tail & rb->mask;
    uint8_t *slotPtr   = rb->storage + (slotIndex * rb->elemSize);

    /* Copy element out of slot. */
    (void)memcpy(elem, slotPtr, rb->elemSize);

    /* Advance tail — signals slot is reclaimed to the producer. */
    atomic_store_explicit(&rb->tail, tail + 1U, memory_order_release);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus RingBuffer_Peek(const RingBuffer *rb, void *elem)
{
    ECUS_CHECK(rb   != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(elem != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(rb->initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (tail == head)
    {
        return ECUS_ERR_UNDERFLOW;
    }

    size_t         slotIndex = tail & rb->mask;
    const uint8_t *slotPtr   = rb->storage + (slotIndex * rb->elemSize);

    (void)memcpy(elem, slotPtr, rb->elemSize);

    /* tail NOT advanced — this is a non-destructive read. */
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void RingBuffer_Flush(RingBuffer *rb)
{
    if ((rb == NULL) || !rb->initialised)
    {
        return;
    }
    /* Reset both indices — safe only when producer/consumer are both idle. */
    atomic_store_explicit(&rb->head, 0U, memory_order_seq_cst);
    atomic_store_explicit(&rb->tail, 0U, memory_order_seq_cst);
    LOG_DEBUG("RingBuffer flushed");
}
