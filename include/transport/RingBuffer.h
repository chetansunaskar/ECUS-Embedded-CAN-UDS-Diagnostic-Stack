/**
 * @file    RingBuffer.h
 * @brief   Lock-free single-producer / single-consumer ring buffer.
 *
 * Used as the Rx and Tx FIFO between the CAN HAL ISR context and the
 * ISO-TP processing task.  In the simulator, "ISR context" maps to a
 * dedicated POSIX thread.
 *
 * Design:
 *  - Power-of-2 capacity enables index masking instead of modulo (faster).
 *  - head/tail are C11 atomic_size_t — no mutex needed for SPSC pattern.
 *  - Elements are stored by VALUE (copy semantics) — no pointer chasing.
 *  - Capacity must be a power of 2 (enforced at init).
 *
 * C concepts demonstrated:
 *   C11 stdatomic (atomic_size_t, atomic_store, atomic_load,
 *   memory_order_acquire / release), generic element storage (void* + size),
 *   power-of-2 bitmask trick, inline status helpers.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include <stdatomic.h>

/* =========================================================================
 * Ring buffer instance
 * ========================================================================= */
typedef struct RingBuffer
{
    uint8_t        *storage;      /**< Flat byte array: capacity * elemSize  */
    size_t          elemSize;     /**< Size of one element in bytes          */
    size_t          capacity;     /**< Max elements (must be power of 2)     */
    size_t          mask;         /**< capacity - 1  (bitmask for wrapping)  */

    atomic_size_t   head;         /**< Write index (producer)                */
    atomic_size_t   tail;         /**< Read  index (consumer)                */

    bool            initialised;
} RingBuffer;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise a ring buffer.
 * @param  rb        Caller-allocated RingBuffer instance.
 * @param  storage   Backing byte array of size (capacity * elemSize).
 * @param  elemSize  Size of each element in bytes.
 * @param  capacity  Max elements; MUST be a power of 2 (e.g. 8, 16, 32).
 * @return ECUS_OK on success.
 */
EcusStatus RingBuffer_Init(RingBuffer *rb,
                            void       *storage,
                            size_t      elemSize,
                            size_t      capacity);

/**
 * @brief  Push one element (copy) into the ring buffer (producer side).
 * @param  rb    Initialised ring buffer.
 * @param  elem  Pointer to element to copy in (elemSize bytes read).
 * @return ECUS_OK, or ECUS_ERR_OVERFLOW if full.
 */
EcusStatus RingBuffer_Push(RingBuffer  *rb,
                            const void  *elem);

/**
 * @brief  Pop one element (copy) from the ring buffer (consumer side).
 * @param  rb    Initialised ring buffer.
 * @param  elem  Pointer to destination buffer (elemSize bytes written).
 * @return ECUS_OK, or ECUS_ERR_UNDERFLOW if empty.
 */
EcusStatus RingBuffer_Pop(RingBuffer *rb,
                           void       *elem);

/**
 * @brief  Peek at the front element without removing it.
 * @param  rb    Initialised ring buffer.
 * @param  elem  Destination buffer.
 * @return ECUS_OK, or ECUS_ERR_UNDERFLOW if empty.
 */
EcusStatus RingBuffer_Peek(const RingBuffer *rb,
                            void             *elem);

/** @return true if the buffer has no elements. */
ECUS_INLINE bool RingBuffer_IsEmpty(const RingBuffer *rb)
{
    return (atomic_load_explicit(&rb->head, memory_order_acquire) ==
            atomic_load_explicit(&rb->tail, memory_order_acquire));
}

/** @return true if the buffer is at full capacity. */
ECUS_INLINE bool RingBuffer_IsFull(const RingBuffer *rb)
{
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return ((head - tail) == rb->capacity);
}

/** @return Number of elements currently stored. */
ECUS_INLINE size_t RingBuffer_Count(const RingBuffer *rb)
{
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return (head - tail);
}

/**
 * @brief  Reset ring buffer to empty (not thread-safe — call only when idle).
 * @param  rb  Initialised ring buffer.
 */
void RingBuffer_Flush(RingBuffer *rb);

#endif /* RING_BUFFER_H */
