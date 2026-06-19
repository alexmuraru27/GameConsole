#pragma once

/*
 * ESP-01S side of the console<->ESP frame protocol (mirror of network.c on the
 * STM32). Reads/writes framed packets over Serial (UART0). The wire format,
 * type IDs and CRC live in the shared header network_protocol.h.
 *
 * The ESP is the slave: it only ever transmits in reply to a command, so the
 * console's polled receiver never overruns.
 */

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "network_protocol.h"

namespace np
{

/* TX assembles the whole frame so the CRC (over type..payload) is one span. */
static uint8_t s_tx[NP_MAX_FRAME];
/* RX lands type+len+payload contiguously at s_rx[0..]; payload at s_rx[3]. */
static uint8_t s_rx[3u + NP_MAX_PAYLOAD];

inline const uint8_t *payload(void) { return &s_rx[3]; }

inline void sendFrame(uint8_t type, const uint8_t *data, uint16_t len)
{
    if (len > NP_MAX_PAYLOAD)
    {
        len = NP_MAX_PAYLOAD;
    }
    s_tx[0] = NP_SYNC0;
    s_tx[1] = NP_SYNC1;
    s_tx[2] = type;
    s_tx[3] = (uint8_t)(len & 0xFFu);
    s_tx[4] = (uint8_t)(len >> 8u);
    if (len > 0u && data != nullptr)
    {
        memcpy(&s_tx[NP_HEADER_SIZE], data, len);
    }
    const uint16_t crc = np_crc16(&s_tx[2], (uint32_t)(3u + len));
    s_tx[NP_HEADER_SIZE + len] = (uint8_t)(crc & 0xFFu);
    s_tx[NP_HEADER_SIZE + len + 1u] = (uint8_t)(crc >> 8u);
    Serial.write(s_tx, NP_FRAME_OVERHEAD + len);
    Serial.flush();
}

inline void sendError(uint8_t code)
{
    sendFrame(NP_RSP_ERR, &code, 1u);
}

/* Send a diagnostic line to the console (forwarded to SWO on the LOGGER_ESP01
 * channel). Only meaningful while handling a command — that's when the console
 * is listening. Payload = [level, message]. */
inline void logf(uint8_t level, const char *fmt, ...)
{
    uint8_t buf[160];
    buf[0] = level;
    va_list ap;
    va_start(ap, fmt);
    const int m = vsnprintf((char *)&buf[1], sizeof(buf) - 1u, fmt, ap);
    va_end(ap);
    uint16_t mlen = (m > 0) ? (uint16_t)m : 0u;
    if (mlen > sizeof(buf) - 1u)
    {
        mlen = sizeof(buf) - 1u; /* vsnprintf truncated */
    }
    sendFrame(NP_RSP_LOG, buf, (uint16_t)(1u + mlen));
}

/* Read one byte before `deadline` (millis). Returns -1 on timeout. */
inline int readByte(uint32_t deadline)
{
    while ((int32_t)(deadline - millis()) > 0)
    {
        if (Serial.available() > 0)
        {
            return Serial.read();
        }
        yield(); /* keep the WiFi stack serviced while waiting */
    }
    return -1;
}

inline bool readN(uint8_t *dst, uint16_t n, uint32_t deadline)
{
    for (uint16_t i = 0u; i < n; i++)
    {
        const int b = readByte(deadline);
        if (b < 0)
        {
            return false;
        }
        dst[i] = (uint8_t)b;
    }
    return true;
}

/* Receive one frame; validate sync + CRC. Returns false on timeout/framing/CRC. */
inline bool readFrame(uint8_t *out_type, uint16_t *out_len, uint32_t timeout_ms)
{
    const uint32_t deadline = millis() + timeout_ms;

    uint8_t sync = 0u;
    while (sync < 2u)
    {
        const int b = readByte(deadline);
        if (b < 0)
        {
            return false;
        }
        if (sync == 0u)
        {
            sync = ((uint8_t)b == NP_SYNC0) ? 1u : 0u;
        }
        else
        {
            sync = ((uint8_t)b == NP_SYNC1) ? 2u : (((uint8_t)b == NP_SYNC0) ? 1u : 0u);
        }
    }

    if (!readN(&s_rx[0], 3u, deadline))
    {
        return false;
    }
    const uint16_t len = (uint16_t)((uint16_t)s_rx[1] | ((uint16_t)s_rx[2] << 8u));
    if (len > NP_MAX_PAYLOAD)
    {
        return false;
    }
    if (!readN(&s_rx[3], len, deadline))
    {
        return false;
    }

    uint8_t crc_bytes[2];
    if (!readN(crc_bytes, 2u, deadline))
    {
        return false;
    }
    const uint16_t crc_rx = (uint16_t)((uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8u));
    if (crc_rx != np_crc16(&s_rx[0], (uint32_t)(3u + len)))
    {
        return false;
    }

    *out_type = s_rx[0];
    *out_len = len;
    return true;
}

} // namespace np
