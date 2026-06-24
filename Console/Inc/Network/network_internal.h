#ifndef __NETWORK_INTERNAL_H
#define __NETWORK_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

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

#endif /* __NETWORK_INTERNAL_H */
