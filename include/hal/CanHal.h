/**
 * @file    CanHal.h
 * @brief   Virtual CAN Hardware Abstraction Layer (HAL).
 *
 * On real hardware (STM32, NXP S32K) this module would wrap FDCAN/msCAN
 * peripheral registers.  In the ECUS simulator it provides:
 *  - A virtual CAN bus backed by two in-process RingBuffers (Rx, Tx).
 *  - A background POSIX thread ("CAN ISR simulator") that drains Tx frames
 *    and echoes them back into the Rx buffer (loopback mode) or routes them
 *    to a registered tester node.
 *  - Callback-based Rx notification (mirrors a real interrupt handler).
 *  - Tx filter: only frames matching the registered ECU CAN ID are accepted.
 *
 * C concepts demonstrated:
 *   function pointer callbacks, opaque HAL state, POSIX threads,
 *   volatile flag for thread control, struct for HAL configuration.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef CAN_HAL_H
#define CAN_HAL_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include "transport/CanFrame.h"
#include "transport/RingBuffer.h"

/* =========================================================================
 * Constants
 * ========================================================================= */
#define CAN_HAL_RX_BUF_DEPTH   16U   /**< Rx ring buffer depth (power-of-2). */
#define CAN_HAL_TX_BUF_DEPTH   16U   /**< Tx ring buffer depth (power-of-2). */

/* =========================================================================
 * Rx callback type
 * Invoked by the virtual ISR thread when a frame arrives on the bus.
 * ========================================================================= */
typedef void (*CanRxCallbackFn)(const CanFrame *frame, void *userCtx);

/* =========================================================================
 * HAL operating mode
 * ========================================================================= */
typedef enum CanHalMode
{
    CAN_HAL_MODE_LOOPBACK = 0U,  /**< Tx frames echo back to Rx (self-test). */
    CAN_HAL_MODE_NORMAL   = 1U   /**< Normal bus (Tx goes out, Rx from bus).  */
} CanHalMode;

/* =========================================================================
 * HAL configuration
 * ========================================================================= */
typedef struct CanHalConfig
{
    uint32_t       ecuCanId;     /**< ECU's own 11-bit CAN node ID.          */
    uint32_t       testerCanId;  /**< Tester tool's CAN ID (for filtering).  */
    CanHalMode     mode;         /**< Loopback or normal.                    */
    uint32_t       busSpeedKbps; /**< Nominal bus speed (diagnostics only).  */
} CanHalConfig;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise the virtual CAN HAL.
 * @param  config  HAL configuration (copied internally).
 * @return ECUS_OK on success.
 */
EcusStatus CanHal_Init(const CanHalConfig *config);

/**
 * @brief  Deinitialise and stop the virtual ISR thread.
 */
void CanHal_Deinit(void);

/**
 * @brief  Register an Rx callback (replaces any previously registered one).
 * @param  cb       Callback function pointer.
 * @param  userCtx  Opaque context pointer passed back to the callback.
 */
void CanHal_RegisterRxCallback(CanRxCallbackFn cb, void *userCtx);

/**
 * @brief  Transmit a CAN frame onto the virtual bus.
 *         The frame is enqueued in the Tx ring buffer; the ISR thread
 *         processes it asynchronously.
 * @param  frame  Frame to transmit.
 * @return ECUS_OK, or ECUS_ERR_HAL_TX_FULL if Tx buffer is full.
 */
EcusStatus CanHal_Transmit(const CanFrame *frame);

/**
 * @brief  Inject a frame directly into the Rx buffer (tester simulation).
 *         Allows the CLI / test harness to simulate incoming CAN messages
 *         without needing a real CAN bus.
 * @param  frame  Frame to inject.
 * @return ECUS_OK on success.
 */
EcusStatus CanHal_InjectRxFrame(const CanFrame *frame);

/**
 * @brief  Poll for a received frame (alternative to callback mode).
 * @param  frame  Output: received frame.
 * @return ECUS_OK if a frame was available, ECUS_ERR_HAL_RX_EMPTY otherwise.
 */
EcusStatus CanHal_Receive(CanFrame *frame);

/**
 * @brief  Return the number of frames in the Rx buffer.
 */
size_t CanHal_RxPending(void);

/**
 * @brief  Return the number of frames in the Tx buffer.
 */
size_t CanHal_TxPending(void);

/**
 * @brief  Print HAL statistics (Tx count, Rx count, drops) to the logger.
 */
void CanHal_PrintStats(void);

#endif /* CAN_HAL_H */
