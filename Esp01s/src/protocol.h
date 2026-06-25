#pragma once

/*
 * ESP-01S side of the console<->ESP frame protocol (mirror of network.c on the
 * STM32). Reads/writes framed packets over Serial (UART0). The wire format,
 * type IDs and CRC live in the shared header network_protocol.h.
 *
 * The ESP is the slave: it only ever transmits in reply to a command, so the
 * console's polled receiver never overruns.
 *
 * The TX/RX frame buffers and the framing routines are defined once in
 * protocol.cpp. This is a real header (declarations only) on purpose: the
 * handlers are split across translation units (wifi/http/espnow_link), so the
 * single shared s_rx — which payload() points into — must have exactly one
 * definition, not a private per-TU copy (which `static` in a header would give).
 */

#include <Arduino.h>
#include "network_protocol.h"

namespace np
{

/* Pointer to the last received frame's payload (valid after readFrame()). */
const uint8_t *payload(void);

/* Assemble and send one framed response; CRC is computed over type..payload. */
void sendFrame(uint8_t type, const uint8_t *data, uint16_t len);

/* Send an NP_RSP_ERR frame carrying a one-byte error code. */
void sendError(uint8_t code);

/* Send a diagnostic line to the console (NP_RSP_LOG, forwarded to SWO on the
 * LOGGER_ESP01 channel). Only meaningful while handling a command — that's when
 * the console is listening. Payload = [level, message]. */
void logf(uint8_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Receive one frame; validate sync + CRC. Returns false on timeout/framing/CRC. */
bool readFrame(uint8_t *out_type, uint16_t *out_len, uint32_t timeout_ms);

} // namespace np
