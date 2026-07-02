/**
 * @file    IsoTp.h
 * @brief   ISO 15765-2 (ISO-TP) transport layer — segmentation and reassembly.
 *
 * ISO 15765-2 defines how UDS diagnostic messages larger than 8 bytes are
 * segmented across multiple CAN frames:
 *
 *   Single Frame (SF):     1 CAN frame  → payload ≤ 7 bytes
 *   First Frame (FF):      opens a multi-frame sequence → payload 8–4095 bytes
 *   Consecutive Frame (CF):continuation frames (SN = 1…F, wrapping)
 *   Flow Control (FC):     receiver → sender, controls pacing (BS, STmin)
 *
 * This module implements:
 *  - Rx reassembly state machine (SF/FF/CF handling).
 *  - Tx segmentation (SF or FF+CF sequence).
 *  - Flow Control generation and consumption.
 *  - P2 / P2* / Cr / Cs timeout tracking (simplified: uses wall-clock ms).
 *
 * C concepts demonstrated:
 *   State machine (enum + switch), bit manipulation (nibble extraction),
 *   function pointer callbacks for PDU delivery, struct for contexts,
 *   timeout tracking with uint32_t counters, buffer-overflow prevention.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#ifndef ISO_TP_H
#define ISO_TP_H

#include "hal/Platform.h"
#include "services/ErrorHandler.h"
#include "transport/CanFrame.h"

/* =========================================================================
 * ISO-TP protocol constants
 * ========================================================================= */
#define ISOTP_SF_MAX_PAYLOAD     7U     /**< SF: max data bytes (DLC=8).  */
#define ISOTP_FF_MAX_LEN      4095U     /**< Max message length (12-bit). */
#define ISOTP_MAX_PAYLOAD_BUF 4096U     /**< Internal reassembly buffer.  */
#define ISOTP_CF_MAX_PAYLOAD     7U     /**< CF: max data bytes.          */

/* Frame type nibbles (upper nibble of PCI byte 0). */
#define ISOTP_PCI_SF   0x00U   /**< Single Frame.           */
#define ISOTP_PCI_FF   0x10U   /**< First Frame.            */
#define ISOTP_PCI_CF   0x20U   /**< Consecutive Frame.      */
#define ISOTP_PCI_FC   0x30U   /**< Flow Control.           */

/* Flow Control status byte (FS). */
#define ISOTP_FC_CTS   0x00U   /**< Continue To Send.       */
#define ISOTP_FC_WAIT  0x01U   /**< Wait.                   */
#define ISOTP_FC_OVFL  0x02U   /**< Overflow / abort.       */

/* Default timing parameters (ms). */
#define ISOTP_DEFAULT_STMIN_MS    0U    /**< 0 ms separation time.       */
#define ISOTP_DEFAULT_BS          0U    /**< Block size 0 = send all CFs.*/
#define ISOTP_TIMEOUT_CR_MS     150U   /**< Cr: max wait for CF.         */
#define ISOTP_TIMEOUT_AS_MS     150U   /**< As: max Tx completion time.  */
#define ISOTP_TIMEOUT_BR_MS      25U   /**< Br: min time before FC.      */
#define ISOTP_TIMEOUT_CS_MS      25U   /**< Cs: max time to send CF.     */

/* =========================================================================
 * Rx state machine states
 * ========================================================================= */
typedef enum IsoTpRxState
{
    ISOTP_RX_IDLE        = 0U,   /**< Waiting for SF or FF.              */
    ISOTP_RX_RECEIVING   = 1U,   /**< Receiving CF sequence.             */
    ISOTP_RX_COMPLETE    = 2U,   /**< Full message assembled.            */
    ISOTP_RX_ERROR       = 3U    /**< Protocol error; reset required.    */
} IsoTpRxState;

/* =========================================================================
 * Tx state machine states
 * ========================================================================= */
typedef enum IsoTpTxState
{
    ISOTP_TX_IDLE        = 0U,   /**< No active Tx.                      */
    ISOTP_TX_WAIT_FC     = 1U,   /**< FF sent; waiting for Flow Control. */
    ISOTP_TX_SENDING_CF  = 2U,   /**< Sending consecutive frames.        */
    ISOTP_TX_COMPLETE    = 3U,   /**< All segments sent.                 */
    ISOTP_TX_ERROR       = 4U    /**< Tx error.                          */
} IsoTpTxState;

/* =========================================================================
 * PDU delivery callback
 * Invoked when a complete UDS PDU has been reassembled.
 * ========================================================================= */
typedef void (*IsoTpPduCallbackFn)(const uint8_t *pdu,
                                    size_t          pduLen,
                                    void           *userCtx);

/* =========================================================================
 * ISO-TP channel context
 * One instance per logical diagnostic channel (ECU ↔ Tester pair).
 * ========================================================================= */
typedef struct IsoTpChannel
{
    /* --- Configuration --- */
    uint32_t          txCanId;       /**< Tx CAN ID (ECU → Tester).       */
    uint32_t          rxCanId;       /**< Rx CAN ID (Tester → ECU).       */
    IsoTpPduCallbackFn pduCallback;  /**< Called with complete PDU.       */
    void             *callbackCtx;   /**< Opaque context for callback.    */

    /* --- Rx state --- */
    IsoTpRxState      rxState;
    uint8_t           rxBuf[ISOTP_MAX_PAYLOAD_BUF];  /**< Reassembly buf.*/
    size_t            rxExpectedLen;  /**< Total expected PDU length.     */
    size_t            rxReceivedLen;  /**< Bytes received so far.         */
    uint8_t           rxNextSN;       /**< Expected consecutive frame SN. */
    uint32_t          rxTimerMs;      /**< Cr timeout counter.            */

    /* --- Tx state --- */
    IsoTpTxState      txState;
    const uint8_t    *txData;        /**< Pointer to data being sent.     */
    size_t            txTotalLen;    /**< Total Tx payload length.        */
    size_t            txSentLen;     /**< Bytes sent so far.              */
    uint8_t           txSN;          /**< Next CF sequence number (1–F). */
    uint8_t           txBlockSize;   /**< BS from received FC.            */
    uint8_t           txSTminMs;     /**< STmin from received FC.         */
    uint8_t           txBlockCount;  /**< CFs sent in current block.      */
    uint32_t          txTimerMs;     /**< As/Cs timeout counter.          */

    bool              initialised;
} IsoTpChannel;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief  Initialise an ISO-TP channel.
 * @param  ch           Caller-allocated channel context.
 * @param  txCanId      ECU's Tx CAN ID (e.g. 0x7E8 for ECU 1).
 * @param  rxCanId      Tester's Tx CAN ID (e.g. 0x7E0).
 * @param  pduCallback  Called with the complete PDU when reassembly finishes.
 * @param  userCtx      Opaque pointer forwarded to pduCallback.
 * @return ECUS_OK on success.
 */
EcusStatus IsoTp_Init(IsoTpChannel      *ch,
                       uint32_t           txCanId,
                       uint32_t           rxCanId,
                       IsoTpPduCallbackFn pduCallback,
                       void              *userCtx);

/**
 * @brief  Feed a received CAN frame into the ISO-TP Rx state machine.
 *         Call this from the CanHal Rx callback for frames matching rxCanId.
 * @param  ch     Initialised channel context.
 * @param  frame  Received CAN frame.
 * @return ECUS_OK, or an ECUS_ERR_ISOTP_* code on protocol error.
 */
EcusStatus IsoTp_ProcessRxFrame(IsoTpChannel   *ch,
                                  const CanFrame *frame);

/**
 * @brief  Transmit a UDS PDU via ISO-TP (segmented if > 7 bytes).
 *         For multi-frame messages, subsequent CFs are sent via IsoTp_TxPump().
 * @param  ch    Initialised channel context.
 * @param  data  PDU data to transmit.
 * @param  len   PDU length in bytes (1–4095).
 * @return ECUS_OK on success.
 */
EcusStatus IsoTp_Transmit(IsoTpChannel  *ch,
                            const uint8_t *data,
                            size_t         len);

/**
 * @brief  Drive the Tx state machine — call periodically (e.g. every 1 ms).
 *         Sends pending CF frames after STmin delay has elapsed.
 * @param  ch         Channel context.
 * @param  elapsedMs  Milliseconds elapsed since last call.
 * @return ECUS_OK, or ECUS_ERR_ISOTP_TIMEOUT_AS on stall.
 */
EcusStatus IsoTp_TxPump(IsoTpChannel *ch, uint32_t elapsedMs);

/**
 * @brief  Reset the channel to IDLE (clears both Rx and Tx state machines).
 * @param  ch  Channel context.
 */
void IsoTp_Reset(IsoTpChannel *ch);

#endif /* ISO_TP_H */
