#ifndef __NETWORK_INTERNAL_H
#define __NETWORK_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* Little-endian field accessors for the wire protocol (the framing header, command
 * payloads). One definition of the byte-shuffle so it isn't open-coded per field. */
static inline uint16_t np_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}
static inline uint32_t np_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}
static inline void np_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8U);
}

/* ------------------------------------------------------------------ *
 *  Frame transport (network_frame.c) — the shared byte/frame layer the
 *  WiFi/HTTP command API (network.c) builds on. npBegin re-asserts the
 *  runtime baud and drains stale RX; npSendFrame/npTransact send a
 *  command (and read one reply); npRxPayload returns the last reply's
 *  payload (valid until the next transaction).
 * ------------------------------------------------------------------ */
void npBegin(void);
void npDrainRx(uint32_t idle_ms, uint32_t max_ms);
bool npSendFrame(uint8_t type, const uint8_t *payload, uint16_t len);
bool npTransact(uint8_t cmd, const uint8_t *payload, uint16_t len,
                uint8_t *rsp_type, uint16_t *rsp_len, uint32_t timeout_ms);
const uint8_t *npRxPayload(void);

/*
 * Internal seam between network.c (which owns the ESP-01 frame master: TX/RX
 * scratch, sync hunting, CRC) and other console drivers that speak the same
 * framed UART protocol — today espnow_link.c. It is deliberately NOT part of the
 * public network.h API the rest of the firmware uses; it exposes just enough of
 * network.c's framing core to run a one-shot command/response transaction without
 * duplicating the framing logic.
 */

/*
 * Send one command frame and read exactly one response, reusing network.c's
 * framing. Lighter than the HTTP path's npBegin(): it re-asserts the runtime baud
 * and drops stale RX bytes *instantly* (the ESP only ever replies to our
 * commands, so the line is idle at a transaction boundary — the heavy idle-wait
 * drain in npBegin is only for post-reset/flash chatter, which the steady
 * per-frame multiplayer poll never sees).
 *
 * On success returns true and, if `rsp_payload` is non-NULL, points it at the
 * response payload inside network.c's RX buffer (valid until the next
 * transaction). Returns false on TX failure, timeout, framing or CRC error.
 */
bool networkTransact(uint8_t cmd, const uint8_t *payload, uint16_t len,
                     uint8_t *rsp_type, uint16_t *rsp_len, const uint8_t **rsp_payload,
                     uint32_t timeout_ms);

/*
 * The same transaction split into two halves so the caller can overlap the
 * round-trip (TX drain + ESP turnaround + reply) with unrelated work instead of
 * busy-waiting on it. networkTransactSend arms the command over the async TX and
 * returns immediately; the reply streams into the RX DMA ring on its own. A later
 * networkTransactCollect reads exactly that one reply (waiting only if it has not
 * fully arrived yet). Strictly one Send is outstanding at a time — still one
 * command, one reply, in order. The per-frame ESP-NOW poll uses these: Send after
 * the game's update(), Collect at the start of the next frame, with render() in
 * between. WiFi/HTTP and the flasher keep using the blocking networkTransact.
 */
bool networkTransactSend(uint8_t cmd, const uint8_t *payload, uint16_t len);
bool networkTransactCollect(uint8_t *rsp_type, uint16_t *rsp_len,
                            const uint8_t **rsp_payload, uint32_t timeout_ms);

#endif /* __NETWORK_INTERNAL_H */
