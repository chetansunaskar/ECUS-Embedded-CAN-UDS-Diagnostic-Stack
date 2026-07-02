/**
 * @file    IsoTp.c
 * @brief   ISO 15765-2 transport protocol — Rx/Tx state machines.
 *
 * C concepts demonstrated:
 *   Multi-state machine (enum + switch/case),
 *   bit manipulation (PCI nibble extraction: frame->data[0] >> 4),
 *   struct-based context (no global state — supports multiple channels),
 *   function pointer callback invocation,
 *   memcpy for safe buffer operations,
 *   bounds checking before every buffer write,
 *   pointer-to-const for read-only data parameters.
 *
 * @author  Chetan S Sunaskar (Embedded Engineer)
 * @version 1.0.0
 */

#include "transport/IsoTp.h"
#include "hal/CanHal.h"
#include "services/Logger.h"
#include "services/EndianUtils.h"

#include <string.h>

/* =========================================================================
 * Private: CAN frame builders
 * ========================================================================= */

/**
 * @brief  Build and transmit a Single Frame.
 *         SF PCI:  Byte 0 = 0x0N where N = payload length (1–7).
 */
static EcusStatus SendSingleFrame(IsoTpChannel  *ch,
                                   const uint8_t *data,
                                   size_t         len)
{
    ECUS_ASSERT(len > 0U);
    ECUS_ASSERT(len <= ISOTP_SF_MAX_PAYLOAD);

    CanFrame frame;
    (void)memset(&frame, 0, sizeof(frame));

    frame.id.raw       = CanFrame_MakeStdId((uint16_t)ch->txCanId);
    frame.type         = CAN_FRAME_DATA;
    frame.dlc          = (uint8_t)(len + 1U);   /* PCI byte + payload */
    frame.data[0]      = (uint8_t)(ISOTP_PCI_SF | (len & 0x0FU));

    (void)memcpy(&frame.data[1], data, len);

    LOG_DEBUG("ISO-TP TX SF: ID=0x%03X  len=%zu  DLC=%u",
              ch->txCanId, len, frame.dlc);

    return CanHal_Transmit(&frame);
}

/**
 * @brief  Build and transmit a First Frame.
 *         FF PCI:  Byte 0 = 0x1N (high nibble of length),
 *                  Byte 1 = low byte of length.
 *                  Bytes 2–7 = first 6 bytes of payload.
 */
static EcusStatus SendFirstFrame(IsoTpChannel  *ch,
                                  const uint8_t *data,
                                  size_t         totalLen)
{
    ECUS_ASSERT(totalLen > ISOTP_SF_MAX_PAYLOAD);
    ECUS_ASSERT(totalLen <= ISOTP_FF_MAX_LEN);

    CanFrame frame;
    (void)memset(&frame, 0, sizeof(frame));

    frame.id.raw  = CanFrame_MakeStdId((uint16_t)ch->txCanId);
    frame.type    = CAN_FRAME_DATA;
    frame.dlc     = ECUS_CAN_DLC_MAX;

    /* PCI: 0x1 in high nibble + 12-bit length. */
    frame.data[0] = (uint8_t)(ISOTP_PCI_FF | ((totalLen >> 8U) & 0x0FU));
    frame.data[1] = (uint8_t)(totalLen & 0xFFU);

    /* First 6 payload bytes. */
    (void)memcpy(&frame.data[2], data, 6U);

    LOG_DEBUG("ISO-TP TX FF: ID=0x%03X  totalLen=%zu", ch->txCanId, totalLen);

    return CanHal_Transmit(&frame);
}

/**
 * @brief  Build and transmit one Consecutive Frame.
 *         CF PCI:  Byte 0 = 0x2N where N = SN (1–F, wraps after F→1).
 */
static EcusStatus SendConsecutiveFrame(IsoTpChannel *ch)
{
    ECUS_ASSERT(ch->txData    != NULL);
    ECUS_ASSERT(ch->txSentLen <  ch->txTotalLen);

    size_t remaining = ch->txTotalLen - ch->txSentLen;
    size_t chunkLen  = ECUS_MIN(remaining, ISOTP_CF_MAX_PAYLOAD);

    CanFrame frame;
    (void)memset(&frame, 0, sizeof(frame));

    frame.id.raw  = CanFrame_MakeStdId((uint16_t)ch->txCanId);
    frame.type    = CAN_FRAME_DATA;
    frame.dlc     = (uint8_t)(chunkLen + 1U);
    frame.data[0] = (uint8_t)(ISOTP_PCI_CF | (ch->txSN & 0x0FU));

    (void)memcpy(&frame.data[1], ch->txData + ch->txSentLen, chunkLen);

    LOG_TRACE("ISO-TP TX CF: SN=%u  offset=%zu  chunk=%zu",
              ch->txSN, ch->txSentLen, chunkLen);

    EcusStatus rc = CanHal_Transmit(&frame);
    if (rc == ECUS_OK)
    {
        ch->txSentLen   += chunkLen;
        ch->txSN         = (uint8_t)((ch->txSN == 0x0FU) ? 0x01U : ch->txSN + 1U);
        ch->txBlockCount++;
    }

    return rc;
}

/**
 * @brief  Build and transmit a Flow Control frame.
 *         FC PCI:  Byte 0 = 0x3N (FS), Byte 1 = BS, Byte 2 = STmin.
 */
static EcusStatus SendFlowControl(const IsoTpChannel *ch, uint8_t fs)
{
    CanFrame frame;
    (void)memset(&frame, 0, sizeof(frame));

    frame.id.raw  = CanFrame_MakeStdId((uint16_t)ch->txCanId);
    frame.type    = CAN_FRAME_DATA;
    frame.dlc     = 3U;
    frame.data[0] = (uint8_t)(ISOTP_PCI_FC | (fs & 0x0FU));
    frame.data[1] = ISOTP_DEFAULT_BS;
    frame.data[2] = ISOTP_DEFAULT_STMIN_MS;

    LOG_DEBUG("ISO-TP TX FC: fs=0x%02X BS=%u STmin=%u ms",
              fs, frame.data[1], frame.data[2]);

    return CanHal_Transmit(&frame);
}

/* =========================================================================
 * Private: Rx frame handlers (one per PCI type)
 * ========================================================================= */

static EcusStatus HandleSingleFrame(IsoTpChannel *ch, const CanFrame *frame)
{
    /* Extract payload length from low nibble of PCI byte. */
    uint8_t sfLen = frame->data[0] & 0x0FU;

    if ((sfLen == 0U) || (sfLen > ISOTP_SF_MAX_PAYLOAD) ||
        (sfLen > (uint8_t)(frame->dlc - 1U)))
    {
        LOG_WARN("ISO-TP RX SF: invalid length %u", sfLen);
        ch->rxState = ISOTP_RX_ERROR;
        return ECUS_ERR_ISOTP_OVERFLOW;
    }

    /* Copy payload into reassembly buffer. */
    (void)memcpy(ch->rxBuf, &frame->data[1], sfLen);
    ch->rxReceivedLen = sfLen;
    ch->rxState       = ISOTP_RX_COMPLETE;

    LOG_DEBUG("ISO-TP RX SF: len=%u  complete", sfLen);

    /* Deliver PDU immediately. */
    if (ch->pduCallback != NULL)
    {
        ch->pduCallback(ch->rxBuf, ch->rxReceivedLen, ch->callbackCtx);
    }

    ch->rxState = ISOTP_RX_IDLE;
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

static EcusStatus HandleFirstFrame(IsoTpChannel *ch, const CanFrame *frame)
{
    if (ch->rxState != ISOTP_RX_IDLE)
    {
        /* Abort previous incomplete reception. */
        LOG_WARN("ISO-TP RX FF: aborting previous incomplete Rx");
        IsoTp_Reset(ch);
    }

    /* Extract 12-bit total length from PCI bytes 0–1. */
    uint16_t totalLen = (uint16_t)(((uint16_t)(frame->data[0] & 0x0FU) << 8U) |
                                    (uint16_t)frame->data[1]);

    if ((totalLen <= ISOTP_SF_MAX_PAYLOAD) || (totalLen > ISOTP_FF_MAX_LEN))
    {
        LOG_ERROR("ISO-TP RX FF: invalid total length %u", totalLen);
        ch->rxState = ISOTP_RX_ERROR;
        return ECUS_ERR_ISOTP_OVERFLOW;
    }

    if (totalLen > ISOTP_MAX_PAYLOAD_BUF)
    {
        /* Send Overflow FC and abort. */
        (void)SendFlowControl(ch, ISOTP_FC_OVFL);
        ch->rxState = ISOTP_RX_ERROR;
        return ECUS_ERR_ISOTP_OVERFLOW;
    }

    ch->rxExpectedLen = (size_t)totalLen;
    ch->rxReceivedLen = 6U;     /* FF carries 6 payload bytes (bytes 2–7). */
    (void)memcpy(ch->rxBuf, &frame->data[2], 6U);

    ch->rxNextSN  = 1U;
    ch->rxTimerMs = 0U;
    ch->rxState   = ISOTP_RX_RECEIVING;

    LOG_DEBUG("ISO-TP RX FF: totalLen=%u  first6Bytes received", totalLen);

    /* Send Flow Control CTS to permit CF transmission. */
    return SendFlowControl(ch, ISOTP_FC_CTS);
}

/* -------------------------------------------------------------------------- */

static EcusStatus HandleConsecutiveFrame(IsoTpChannel *ch, const CanFrame *frame)
{
    if (ch->rxState != ISOTP_RX_RECEIVING)
    {
        LOG_WARN("ISO-TP RX CF: unexpected CF in state %u", ch->rxState);
        return ECUS_ERR_ISOTP_UNEXP_PDU;
    }

    /* Validate sequence number (low nibble). */
    uint8_t sn = frame->data[0] & 0x0FU;
    if (sn != (ch->rxNextSN & 0x0FU))
    {
        LOG_ERROR("ISO-TP RX CF: SN mismatch (expected %u got %u)",
                  ch->rxNextSN & 0x0FU, sn);
        ch->rxState = ISOTP_RX_ERROR;
        return ECUS_ERR_ISOTP_WRONG_SN;
    }

    /* Reset Cr timer on each received CF. */
    ch->rxTimerMs = 0U;

    size_t remaining = ch->rxExpectedLen - ch->rxReceivedLen;
    size_t chunkLen  = ECUS_MIN(remaining, ISOTP_CF_MAX_PAYLOAD);

    /* Bounds check before writing into rxBuf. */
    if ((ch->rxReceivedLen + chunkLen) > ISOTP_MAX_PAYLOAD_BUF)
    {
        ch->rxState = ISOTP_RX_ERROR;
        return ECUS_ERR_ISOTP_OVERFLOW;
    }

    (void)memcpy(ch->rxBuf + ch->rxReceivedLen, &frame->data[1], chunkLen);
    ch->rxReceivedLen += chunkLen;

    /* Advance SN (wraps 0xF → 0x1, not 0x0). */
    ch->rxNextSN = (uint8_t)((sn == 0x0FU) ? 0x01U : sn + 1U);

    LOG_TRACE("ISO-TP RX CF: SN=%u  received=%zu / %zu",
              sn, ch->rxReceivedLen, ch->rxExpectedLen);

    if (ch->rxReceivedLen >= ch->rxExpectedLen)
    {
        ch->rxState = ISOTP_RX_COMPLETE;
        LOG_DEBUG("ISO-TP RX: message complete (%zu bytes)", ch->rxReceivedLen);

        if (ch->pduCallback != NULL)
        {
            ch->pduCallback(ch->rxBuf, ch->rxReceivedLen, ch->callbackCtx);
        }

        ch->rxState = ISOTP_RX_IDLE;
    }

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

static EcusStatus HandleFlowControl(IsoTpChannel *ch, const CanFrame *frame)
{
    if (ch->txState != ISOTP_TX_WAIT_FC)
    {
        LOG_WARN("ISO-TP RX FC: unexpected FC in Tx state %u", ch->txState);
        return ECUS_ERR_ISOTP_UNEXP_PDU;
    }

    uint8_t fs    = frame->data[0] & 0x0FU;
    uint8_t bs    = frame->data[1];
    uint8_t stmin = frame->data[2];

    LOG_DEBUG("ISO-TP RX FC: FS=%u  BS=%u  STmin=%u ms", fs, bs, stmin);

    switch (fs)
    {
        case ISOTP_FC_CTS:
            ch->txBlockSize  = bs;
            ch->txSTminMs    = stmin;
            ch->txBlockCount = 0U;
            ch->txTimerMs    = 0U;
            ch->txState      = ISOTP_TX_SENDING_CF;
            break;

        case ISOTP_FC_WAIT:
            /* Remain in WAIT_FC — tester will send another FC. */
            LOG_DEBUG("ISO-TP TX: FC WAIT received");
            break;

        case ISOTP_FC_OVFL:
            LOG_ERROR("ISO-TP TX: receiver overflow — aborting");
            ch->txState = ISOTP_TX_ERROR;
            return ECUS_ERR_ISOTP_OVERFLOW;

        default:
            ch->txState = ISOTP_TX_ERROR;
            return ECUS_ERR_ISOTP_UNEXP_PDU;
    }

    return ECUS_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

EcusStatus IsoTp_Init(IsoTpChannel      *ch,
                       uint32_t           txCanId,
                       uint32_t           rxCanId,
                       IsoTpPduCallbackFn pduCallback,
                       void              *userCtx)
{
    ECUS_CHECK(ch != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);

    (void)memset(ch, 0, sizeof(IsoTpChannel));

    ch->txCanId      = txCanId;
    ch->rxCanId      = rxCanId;
    ch->pduCallback  = pduCallback;
    ch->callbackCtx  = userCtx;
    ch->rxState      = ISOTP_RX_IDLE;
    ch->txState      = ISOTP_TX_IDLE;
    ch->initialised  = true;

    LOG_INFO("ISO-TP channel init: TX_ID=0x%03X  RX_ID=0x%03X",
             txCanId, rxCanId);
    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

EcusStatus IsoTp_ProcessRxFrame(IsoTpChannel *ch, const CanFrame *frame)
{
    ECUS_CHECK(ch    != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(frame != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(ch->initialised, ECUS_ERR_NOT_READY, return ECUS_ERR_NOT_READY);

    /* Filter: only process frames addressed to this channel's rxCanId. */
    if (CanFrame_GetId(frame) != ch->rxCanId)
    {
        return ECUS_OK;   /* Not for us — silently ignore. */
    }

    if (frame->dlc < 1U)
    {
        return ECUS_ERR_ISOTP_UNEXP_PDU;
    }

    /* Dispatch on PCI type (upper nibble of byte 0). */
    uint8_t pciType = frame->data[0] & 0xF0U;

    switch (pciType)
    {
        case ISOTP_PCI_SF:  return HandleSingleFrame(ch, frame);
        case ISOTP_PCI_FF:  return HandleFirstFrame(ch, frame);
        case ISOTP_PCI_CF:  return HandleConsecutiveFrame(ch, frame);
        case ISOTP_PCI_FC:  return HandleFlowControl(ch, frame);
        default:
            LOG_WARN("ISO-TP RX: unknown PCI type 0x%02X", pciType);
            return ECUS_ERR_ISOTP_UNEXP_PDU;
    }
}

/* -------------------------------------------------------------------------- */

EcusStatus IsoTp_Transmit(IsoTpChannel  *ch,
                            const uint8_t *data,
                            size_t         len)
{
    ECUS_CHECK(ch   != NULL, ECUS_ERR_NULL_PTR,      return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(data != NULL, ECUS_ERR_NULL_PTR,      return ECUS_ERR_NULL_PTR);
    ECUS_CHECK(len  >  0U,   ECUS_ERR_INVALID_PARAM, return ECUS_ERR_INVALID_PARAM);
    ECUS_CHECK(len  <= ISOTP_FF_MAX_LEN, ECUS_ERR_OVERFLOW, return ECUS_ERR_OVERFLOW);
    ECUS_CHECK(ch->initialised, ECUS_ERR_NOT_READY,  return ECUS_ERR_NOT_READY);

    if (len <= ISOTP_SF_MAX_PAYLOAD)
    {
        /* Single frame — no state machine needed. */
        return SendSingleFrame(ch, data, len);
    }

    /* Multi-frame: send FF, then wait for FC before sending CFs. */
    ch->txData       = data;
    ch->txTotalLen   = len;
    ch->txSentLen    = 6U;   /* FF carries first 6 bytes. */
    ch->txSN         = 1U;
    ch->txBlockCount = 0U;
    ch->txTimerMs    = 0U;
    ch->txState      = ISOTP_TX_WAIT_FC;

    return SendFirstFrame(ch, data, len);
}

/* -------------------------------------------------------------------------- */

EcusStatus IsoTp_TxPump(IsoTpChannel *ch, uint32_t elapsedMs)
{
    ECUS_CHECK(ch != NULL, ECUS_ERR_NULL_PTR, return ECUS_ERR_NULL_PTR);

    if (ch->txState != ISOTP_TX_SENDING_CF)
    {
        /* Check As timeout while waiting for FC. */
        if (ch->txState == ISOTP_TX_WAIT_FC)
        {
            ch->txTimerMs += elapsedMs;
            if (ch->txTimerMs > ISOTP_TIMEOUT_AS_MS)
            {
                LOG_ERROR("ISO-TP TX: As timeout waiting for FC");
                ch->txState = ISOTP_TX_ERROR;
                return ECUS_ERR_ISOTP_TIMEOUT_AS;
            }
        }
        return ECUS_OK;
    }

    ch->txTimerMs += elapsedMs;

    /* Honour STmin inter-frame delay. */
    if (ch->txTimerMs < (uint32_t)ch->txSTminMs)
    {
        return ECUS_OK;
    }

    ch->txTimerMs = 0U;

    /* Send one (or all remaining) CFs. */
    while (ch->txSentLen < ch->txTotalLen)
    {
        EcusStatus rc = SendConsecutiveFrame(ch);
        if (rc != ECUS_OK)
        {
            ch->txState = ISOTP_TX_ERROR;
            return rc;
        }

        /* Block size limit: pause after BS consecutive frames. */
        if ((ch->txBlockSize > 0U) &&
            (ch->txBlockCount >= ch->txBlockSize))
        {
            ch->txBlockCount = 0U;
            ch->txState      = ISOTP_TX_WAIT_FC;
            return ECUS_OK;
        }
    }

    ch->txState = ISOTP_TX_COMPLETE;
    LOG_DEBUG("ISO-TP TX: all %zu bytes sent", ch->txTotalLen);

    return ECUS_OK;
}

/* -------------------------------------------------------------------------- */

void IsoTp_Reset(IsoTpChannel *ch)
{
    if (ch == NULL) { return; }

    bool init = ch->initialised;
    uint32_t txId = ch->txCanId;
    uint32_t rxId = ch->rxCanId;
    IsoTpPduCallbackFn cb  = ch->pduCallback;
    void              *ctx = ch->callbackCtx;

    (void)memset(ch, 0, sizeof(IsoTpChannel));

    ch->txCanId     = txId;
    ch->rxCanId     = rxId;
    ch->pduCallback = cb;
    ch->callbackCtx = ctx;
    ch->rxState     = ISOTP_RX_IDLE;
    ch->txState     = ISOTP_TX_IDLE;
    ch->initialised = init;

    LOG_DEBUG("ISO-TP channel reset");
}
