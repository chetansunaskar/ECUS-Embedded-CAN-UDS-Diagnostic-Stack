/**
 * @file    CanHal.c
 * @brief   Virtual CAN HAL implementation — POSIX thread simulates CAN ISR.
 *
 * C concepts demonstrated:
 *   POSIX threads (pthread_create, pthread_join, pthread_mutex),
 *   volatile flag for clean thread shutdown,
 *   function pointer callback dispatch,
 *   struct for module state encapsulation,
 *   atomic counters for stats,
 *   ring buffer usage (push/pop),
 *   nanosleep() for ISR timing simulation.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "hal/CanHal.h"
#include "services/Logger.h"

#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>

/* =========================================================================
 * Private: backing storage for ring buffers
 * ========================================================================= */
static CanFrame s_rxStorage[CAN_HAL_RX_BUF_DEPTH];
static CanFrame s_txStorage[CAN_HAL_TX_BUF_DEPTH];

/* =========================================================================
 * Module state struct — all HAL state in one place
 * ========================================================================= */
typedef struct CanHalState
{
    CanHalConfig    config;
    RingBuffer      rxBuf;
    RingBuffer      txBuf;

    CanRxCallbackFn rxCallback;
    void           *rxCallbackCtx;

    pthread_t       isrThread;
    pthread_mutex_t callbackMutex;   /* Protect callback ptr during dispatch */
    volatile bool   isrRunning;      /* volatile: read by both threads       */

    atomic_uint_fast32_t statTxCount;
    atomic_uint_fast32_t statRxCount;
    atomic_uint_fast32_t statDropped;

    bool initialised;
} CanHalState;

static CanHalState s_hal;   /* zero-initialised at program startup */

/* =========================================================================
 * Private: get a monotonic timestamp in milliseconds
 * ========================================================================= */
static uint32_t GetTimestampMs(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL +
                      (uint64_t)ts.tv_nsec / 1000000ULL);
}

/* =========================================================================
 * Private: virtual ISR thread
 *
 * Wakes at ~1 ms intervals, drains the Tx ring buffer, and:
 *  - In LOOPBACK mode: echoes each Tx frame back into the Rx buffer.
 *  - In NORMAL mode: just counts transmitted frames (no real bus).
 * Then invokes the registered Rx callback for each Rx frame available.
 * ========================================================================= */
static void *CanIsrThread(void *arg)
{
    ECUS_UNUSED_PARAM(arg);

    struct timespec sleepTime;
    sleepTime.tv_sec  = 0;
    sleepTime.tv_nsec = 1000000L;   /* 1 ms */

    LOG_DEBUG("CAN HAL: ISR thread started (mode=%s)",
              (s_hal.config.mode == CAN_HAL_MODE_LOOPBACK) ? "LOOPBACK" : "NORMAL");

    while (s_hal.isrRunning)
    {
        CanFrame frame;

        /* --- Drain Tx buffer --- */
        while (RingBuffer_Pop(&s_hal.txBuf, &frame) == ECUS_OK)
        {
            frame.timestamp_ms = GetTimestampMs();
            atomic_fetch_add(&s_hal.statTxCount, 1U);

            LOG_TRACE("CAN TX: ID=0x%03X DLC=%u",
                      CanFrame_GetId(&frame), frame.dlc);

            if (s_hal.config.mode == CAN_HAL_MODE_LOOPBACK)
            {
                /* Echo frame back to Rx (simulates bus round-trip). */
                if (RingBuffer_Push(&s_hal.rxBuf, &frame) != ECUS_OK)
                {
                    atomic_fetch_add(&s_hal.statDropped, 1U);
                    LOG_WARN("CAN HAL: Rx buffer full — frame dropped (ID=0x%03X)",
                             CanFrame_GetId(&frame));
                }
            }
        }

        /* --- Dispatch Rx frames to callback --- */
        while (RingBuffer_Pop(&s_hal.rxBuf, &frame) == ECUS_OK)
        {
            atomic_fetch_add(&s_hal.statRxCount, 1U);

            LOG_TRACE("CAN RX: ID=0x%03X DLC=%u ts=%u ms",
                      CanFrame_GetId(&frame), frame.dlc, frame.timestamp_ms);

            /* Dispatch callback under mutex to allow safe deregistration. */
            (void)pthread_mutex_lock(&s_hal.callbackMutex);
            if (s_hal.rxCallback != NULL)
            {
                s_hal.rxCallback(&frame, s_hal.rxCallbackCtx);
            }
            (void)pthread_mutex_unlock(&s_hal.callbackMutex);
        }

        /* Sleep to yield CPU — simulates hardware interrupt cadence. */
        (void)nanosleep(&sleepTime, NULL);
    }

    LOG_DEBUG("CAN HAL: ISR thread stopped");
    return NULL;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus CanHal_Init(const CanHalConfig *config)
{
    ECUS_CHECK(config != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(!s_hal.initialised, ECUS_ERR_ALREADY_INIT,
               return ECUS_ERR_ALREADY_INIT);

    /* Copy config. */
    (void)memcpy(&s_hal.config, config, sizeof(CanHalConfig));

    /* Initialise ring buffers. */
    ECUS_PROPAGATE(RingBuffer_Init(&s_hal.rxBuf,
                                   s_rxStorage,
                                   sizeof(CanFrame),
                                   CAN_HAL_RX_BUF_DEPTH));

    ECUS_PROPAGATE(RingBuffer_Init(&s_hal.txBuf,
                                   s_txStorage,
                                   sizeof(CanFrame),
                                   CAN_HAL_TX_BUF_DEPTH));

    /* Initialise stats. */
    atomic_store(&s_hal.statTxCount, 0U);
    atomic_store(&s_hal.statRxCount, 0U);
    atomic_store(&s_hal.statDropped, 0U);

    /* Initialise mutex. */
    if (pthread_mutex_init(&s_hal.callbackMutex, NULL) != 0)
    {
        return ECUS_ERR_HAL_INIT;
    }

    /* Start ISR simulation thread. */
    s_hal.isrRunning = true;
    if (pthread_create(&s_hal.isrThread, NULL, CanIsrThread, NULL) != 0)
    {
        s_hal.isrRunning = false;
        (void)pthread_mutex_destroy(&s_hal.callbackMutex);
        return ECUS_ERR_HAL_INIT;
    }

    s_hal.initialised = true;

    LOG_INFO("CAN HAL initialised: ECU_ID=0x%03X  Tester_ID=0x%03X  Mode=%s  Speed=%u kbps",
             config->ecuCanId,
             config->testerCanId,
             (config->mode == CAN_HAL_MODE_LOOPBACK) ? "LOOPBACK" : "NORMAL",
             config->busSpeedKbps);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void CanHal_Deinit(void)
{
    if (!s_hal.initialised)
    {
        return;
    }

    /* Signal ISR thread to stop and wait for it. */
    s_hal.isrRunning = false;
    (void)pthread_join(s_hal.isrThread, NULL);

    (void)pthread_mutex_destroy(&s_hal.callbackMutex);

    s_hal.initialised = false;
    LOG_INFO("CAN HAL deinitialised");
}

/* -------------------------------------------------------------------------- */

void CanHal_RegisterRxCallback(CanRxCallbackFn cb, void *userCtx)
{
    ECUS_ASSERT(s_hal.initialised);

    (void)pthread_mutex_lock(&s_hal.callbackMutex);
    s_hal.rxCallback    = cb;
    s_hal.rxCallbackCtx = userCtx;
    (void)pthread_mutex_unlock(&s_hal.callbackMutex);

    LOG_DEBUG("CAN HAL: Rx callback %s", (cb != NULL) ? "registered" : "cleared");
}

/* -------------------------------------------------------------------------- */

EcusStatus CanHal_Transmit(const CanFrame *frame)
{
    ECUS_CHECK(frame != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(s_hal.initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);
    ECUS_CHECK(frame->dlc <= ECUS_CAN_DLC_MAX, ECUS_ERR_INVALID_PARAM,
               return ECUS_ERR_INVALID_PARAM);

    EcusStatus rc = RingBuffer_Push(&s_hal.txBuf, frame);
    if (rc != ECUS_OK)
    {
        atomic_fetch_add(&s_hal.statDropped, 1U);
        LOG_WARN("CAN HAL: Tx buffer full — frame dropped");
        return ECUS_ERR_HAL_TX_FULL;
    }

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus CanHal_InjectRxFrame(const CanFrame *frame)
{
    ECUS_CHECK(frame != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(s_hal.initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    CanFrame injectFrame;
    (void)memcpy(&injectFrame, frame, sizeof(CanFrame));
    injectFrame.timestamp_ms = GetTimestampMs();

    EcusStatus rc = RingBuffer_Push(&s_hal.rxBuf, &injectFrame);
    if (rc != ECUS_OK)
    {
        atomic_fetch_add(&s_hal.statDropped, 1U);
        return ECUS_ERR_HAL_RX_EMPTY;
    }

    LOG_TRACE("CAN HAL: Injected Rx frame ID=0x%03X DLC=%u",
              CanFrame_GetId(frame), frame->dlc);
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus CanHal_Receive(CanFrame *frame)
{
    ECUS_CHECK(frame != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(s_hal.initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    return RingBuffer_Pop(&s_hal.rxBuf, frame);
}

/* -------------------------------------------------------------------------- */

size_t CanHal_RxPending(void) { return RingBuffer_Count(&s_hal.rxBuf); }
size_t CanHal_TxPending(void) { return RingBuffer_Count(&s_hal.txBuf); }

/* -------------------------------------------------------------------------- */

void CanHal_PrintStats(void)
{
    LOG_INFO("CAN HAL Stats: TX=%u  RX=%u  Dropped=%u  RxPending=%zu  TxPending=%zu",
             atomic_load(&s_hal.statTxCount),
             atomic_load(&s_hal.statRxCount),
             atomic_load(&s_hal.statDropped),
             CanHal_RxPending(),
             CanHal_TxPending());
}
