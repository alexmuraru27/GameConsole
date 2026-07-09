#include "Network/network.h"
#include <stm32f407xx.h>
#include "Network/network_internal.h"

#include <string.h>

#include "Peripherals/usart.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Logger/logger.h"

/*
 * Frame transport for the ESP-01S link — the shared byte/frame machinery that both
 * the WiFi/HTTP command API (network.c) and the ESP-NOW transport (espnow_link.c)
 * run over. Layout (network_protocol.h):
 *   0xA5 0x5A | type | len:u16 LE | payload | crc16:u16 LE
 *
 * Split out of network.c so "how a frame moves" lives in one place, behind the seam
 * declared in network_internal.h; the command layer builds on npBegin/npSendFrame/
 * npTransact and reads the reply via npRxPayload().
 */

#define NP_TX_TIMEOUT 1000U

/* TX scratch: full frame. RX scratch: [type,len_lo,len_hi, payload...] for CRC. */
static uint8_t s_tx[NP_MAX_FRAME];
static uint8_t s_rx[3U + NP_MAX_PAYLOAD];

const uint8_t *npRxPayload(void)
{
    return &s_rx[3]; /* the last received frame's payload (after type + len) */
}

/* Discard bytes the ESP left on the line (post-reset ROM/boot chatter, a late
 * reply after a timeout) so a transaction starts from a clean, idle line. The
 * STM32 RX is polled with only the 1-byte DR + shift register behind it, so any
 * unread leftover overruns and desyncs framing. Reads until the line is quiet
 * for `idle_ms`, or `max_ms` elapses. */
void npDrainRx(uint32_t idle_ms, uint32_t max_ms)
{
    const uint32_t hard_deadline = getSysTime() + max_ms;
    while (getSysTime() < hard_deadline)
    {
        if (usartReadByte(idle_ms) < 0)
        {
            break; /* no byte for idle_ms — line is drained */
        }
    }
    usartFlushRx();
}

void npBegin(void)
{
    /* Re-assert the runtime baud — the flasher may have left USART1 at 115200. */
    usartSetBaud(NETWORK_UART_BAUD);
    /* Clear any stale/late bytes so this command's response frames cleanly. */
    npDrainRx(3U, 40U);
}

/* Build a command frame into s_tx; returns the total wire length, or 0 if the
 * payload is too big. */
static uint16_t npBuildFrame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    if (len > NP_MAX_PAYLOAD)
    {
        return 0U;
    }

    s_tx[0] = NP_SYNC0;
    s_tx[1] = NP_SYNC1;
    s_tx[2] = type;
    np_wr16(&s_tx[3], len);
    if (len > 0U && payload != NULL)
    {
        memcpy(&s_tx[NP_HEADER_SIZE], payload, len);
    }
    const uint16_t crc = np_crc16(&s_tx[2], (uint32_t)(1U + 2U + len)); /* type..payload */
    np_wr16(&s_tx[NP_HEADER_SIZE + len], crc);

    return (uint16_t)(NP_FRAME_OVERHEAD + len);
}

bool npSendFrame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    const uint16_t total = npBuildFrame(type, payload, len);
    return (total != 0U) && usartWriteBytes(s_tx, total, NP_TX_TIMEOUT);
}

/* Async variant: arm the TX DMA and return immediately, leaving the frame to
 * drain on its own. s_tx must not be rebuilt until the transfer completes — the
 * next send drains it first (usartWriteBytesStart), and the per-frame poll rebuilds
 * it only once per frame, after the prior reply has been collected. */
static bool npSendFrameAsync(uint8_t type, const uint8_t *payload, uint16_t len)
{
    const uint16_t total = npBuildFrame(type, payload, len);
    return (total != 0U) && usartWriteBytesStart(s_tx, total, NP_TX_TIMEOUT);
}

/* Read `n` bytes into `dst` before `deadline`. */
static bool npReadExact(uint8_t *dst, uint16_t n, uint32_t deadline)
{
    for (uint16_t i = 0U; i < n; i++)
    {
        const uint32_t now = getSysTime();
        if (now >= deadline)
        {
            return false;
        }
        const int b = usartReadByte(deadline - now);
        if (b < 0)
        {
            return false;
        }
        dst[i] = (uint8_t)b;
    }
    return true;
}

/* Buffered diagnostics from the ESP (NP_RSP_LOG). They are collected during a
 * receive and flushed to SWO only after the response frame is in, so the slow
 * SWO printf never stalls the polled receiver between back-to-back ESP frames. */
#define ESP_LOG_MAX 16U
#define ESP_LOG_MSG_MAX 96U
static uint8_t s_esp_log_level[ESP_LOG_MAX];
static char s_esp_log_msg[ESP_LOG_MAX][ESP_LOG_MSG_MAX];
static uint8_t s_esp_log_count;

static void espForwardLog(uint8_t level, const char *msg)
{
    switch (level)
    {
    case NP_LOG_ERROR:
        LOGGER_LOG_ERROR(LOGGER_ESP01, "%s", msg);
        break;
    case NP_LOG_WARN:
        LOGGER_LOG_WARN(LOGGER_ESP01, "%s", msg);
        break;
    case NP_LOG_DEBUG:
        LOGGER_LOG_DEBUG(LOGGER_ESP01, "%s", msg);
        break;
    default:
        LOGGER_LOG_INFO(LOGGER_ESP01, "%s", msg);
        break;
    }
}

/* Push buffered ESP logs to SWO. Done once the ESP has stopped sending (on a
 * good response OR a failed/timed-out transaction) so partial diagnostics from a
 * failure aren't lost. */
static void flushEspLogs(void)
{
    for (uint8_t i = 0U; i < s_esp_log_count; i++)
    {
        espForwardLog(s_esp_log_level[i], s_esp_log_msg[i]);
    }
    s_esp_log_count = 0U;
}

/* Read one frame off the wire into s_rx (payload at &s_rx[3]), validating sync +
 * CRC. Returns the frame type, or -1 on timeout / framing / CRC error. */
static int npReadRawFrame(uint16_t *out_len, uint32_t deadline)
{
    /* Hunt for the sync word, tolerating leading noise. */
    uint8_t sync_state = 0U;
    while (sync_state < 2U)
    {
        const uint32_t now = getSysTime();
        if (now >= deadline)
        {
            return -1;
        }
        const int b = usartReadByte(deadline - now);
        if (b < 0)
        {
            return -1;
        }
        if (sync_state == 0U)
        {
            sync_state = ((uint8_t)b == NP_SYNC0) ? 1U : 0U;
        }
        else /* saw SYNC0 */
        {
            sync_state = ((uint8_t)b == NP_SYNC1) ? 2U : (((uint8_t)b == NP_SYNC0) ? 1U : 0U);
        }
    }

    /* type + len (contiguous at s_rx[0..2] for the CRC span). */
    if (!npReadExact(&s_rx[0], 3U, deadline))
    {
        return -1;
    }
    const uint16_t len = np_rd16(&s_rx[1]);
    if (len > NP_MAX_PAYLOAD || !npReadExact(&s_rx[3], len, deadline))
    {
        return -1;
    }

    uint8_t crc_bytes[2];
    if (!npReadExact(crc_bytes, 2U, deadline))
    {
        return -1;
    }
    const uint16_t crc_rx = np_rd16(crc_bytes);
    if (crc_rx != np_crc16(&s_rx[0], (uint32_t)(3U + len)))
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "frame crc mismatch");
        return -1;
    }

    *out_len = len;
    return s_rx[0];
}

/* Receive the response to a command. NP_RSP_LOG frames that precede it are
 * buffered and flushed to SWO once the response is fully read. */
static bool npRecvFrame(uint8_t *out_type, uint16_t *out_len, uint32_t timeout_ms)
{
    const uint32_t deadline = getSysTime() + timeout_ms;
    s_esp_log_count = 0U;

    for (;;)
    {
        uint16_t len = 0U;
        const int type = npReadRawFrame(&len, deadline);
        if (type < 0)
        {
            flushEspLogs(); /* surface any diagnostics that arrived before the failure */
            return false;
        }

        if (type == NP_RSP_LOG)
        {
            /* Buffer the message (level byte + text); flush later. */
            if (s_esp_log_count < ESP_LOG_MAX && len >= 1U)
            {
                uint16_t mlen = (uint16_t)(len - 1U);
                if (mlen > ESP_LOG_MSG_MAX - 1U)
                {
                    mlen = ESP_LOG_MSG_MAX - 1U;
                }
                s_esp_log_level[s_esp_log_count] = s_rx[3];
                memcpy(s_esp_log_msg[s_esp_log_count], &s_rx[4], mlen);
                s_esp_log_msg[s_esp_log_count][mlen] = '\0';
                s_esp_log_count++;
            }
            continue;
        }

        /* Response in hand — the ESP is done sending, so flush logs to SWO now. */
        flushEspLogs();
        *out_type = (uint8_t)type;
        *out_len = len;
        return true;
    }
}

/* Send a command and receive its response. Returns false on any failure. */
bool npTransact(uint8_t cmd, const uint8_t *payload, uint16_t len,
                uint8_t *rsp_type, uint16_t *rsp_len, uint32_t timeout_ms)
{
    if (!npSendFrame(cmd, payload, len))
    {
        return false;
    }
    return npRecvFrame(rsp_type, rsp_len, timeout_ms);
}

/* Internal seam (network_internal.h): a one-shot transaction for other drivers
 * that speak this protocol (espnow_link.c), reusing the framing above. Uses the
 * instant RX flush rather than npBegin's idle-wait drain — see the header. */
bool networkTransact(uint8_t cmd, const uint8_t *payload, uint16_t len,
                     uint8_t *rsp_type, uint16_t *rsp_len, const uint8_t **rsp_payload,
                     uint32_t timeout_ms)
{
    usartSetBaud(NETWORK_UART_BAUD);
    usartFlushRx(); /* drop stale bytes instantly; the line is idle at a boundary */

    if (!npTransact(cmd, payload, len, rsp_type, rsp_len, timeout_ms))
    {
        return false;
    }
    if (rsp_payload != NULL)
    {
        *rsp_payload = &s_rx[3]; /* payload sits after [type, len_lo, len_hi] */
    }
    return true;
}

/* Internal seam: the SEND half of a transaction (network_internal.h). Flushes any
 * stale RX, then arms the command frame over the async TX and returns at once —
 * the reply lands in the RX ring on its own while the caller does other work. The
 * line is idle at this boundary (the previous reply was already collected), so the
 * flush only drops strays, never the upcoming reply. Pair with networkTransactCollect.
 */
bool networkTransactSend(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    usartSetBaud(NETWORK_UART_BAUD);
    usartFlushRx(); /* drop stale bytes instantly; the line is idle at a boundary */
    return npSendFrameAsync(cmd, payload, len);
}

/* Internal seam: the COLLECT half (network_internal.h). Ensures the request has
 * fully left (a reply cannot exist until it has — normally already drained during
 * the overlapped work, so this returns at once), then reads exactly one response
 * frame from the RX ring. On success points `rsp_payload` at the payload inside
 * the RX buffer (valid until the next transaction). False on TX drain / timeout /
 * framing / CRC error. */
bool networkTransactCollect(uint8_t *rsp_type, uint16_t *rsp_len,
                            const uint8_t **rsp_payload, uint32_t timeout_ms)
{
    if (!usartTxWait(NP_TX_TIMEOUT))
    {
        return false;
    }
    if (!npRecvFrame(rsp_type, rsp_len, timeout_ms))
    {
        return false;
    }
    if (rsp_payload != NULL)
    {
        *rsp_payload = &s_rx[3];
    }
    return true;
}
