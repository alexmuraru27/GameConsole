#ifndef NETWORK_PROTOCOL_H
#define NETWORK_PROTOCOL_H

/*
 * Console <-> ESP-01S serial protocol.
 *
 * Single source of truth shared by BOTH sides of the link:
 *   - the STM32 console firmware  (Console/Src/Network/network.c — the master), and
 *   - the ESP-01S firmware        (Esp01s/, built with PlatformIO — the slave).
 *
 * The console is the MASTER: it sends one command frame and reads exactly one
 * response frame before doing anything else (e.g. an SD write). Because the
 * slave only ever transmits in reply, a polled receiver on the STM32 never
 * overruns — there is no unsolicited stream to keep up with.
 *
 * It exists to let the console pull updates (games/.paks, the ESP firmware) from
 * the PC update server over WiFi: scan, connect, then HTTP-GET files in chunks.
 *
 * Frame:
 *   0xA5 0x5A | type:u8 | len:u16 LE | payload[len] | crc16:u16 LE
 *   crc16 (CRC-16-CCITT, init 0xFFFF, poly 0x1021) covers type..payload.
 */

#include <stdint.h>

/* Runtime UART baud for the console<->ESP link on USART1. 115200 — same rate as
 * firmware flashing (the ESP ROM bootloader rate), so the link runs at one
 * consistent speed. The console receives via circular DMA (usart.c), which would
 * allow far higher rates, but 115200 is the chosen reliable default. */
#define NETWORK_UART_BAUD 115200u

/* Bumped whenever the wire format below changes incompatibly. */
#define NETWORK_PROTOCOL_VERSION 2u

/* ---- Framing ---- */
#define NP_SYNC0 0xA5u
#define NP_SYNC1 0x5Au
#define NP_MAX_PAYLOAD 1024u       /* largest payload (an HTTP_DATA chunk)        */
#define NP_HEADER_SIZE 5u          /* sync(2) + type(1) + len(2)                  */
#define NP_CRC_SIZE 2u             /* trailing crc16                             */
#define NP_FRAME_OVERHEAD (NP_HEADER_SIZE + NP_CRC_SIZE)
#define NP_MAX_FRAME (NP_FRAME_OVERHEAD + NP_MAX_PAYLOAD)

/* String field limits (WPA2: SSID <= 32, PSK <= 63). */
#define NP_SSID_MAX 32u
#define NP_PASS_MAX 63u
#define NP_URL_MAX 256u

/* Frame types. Responses have the high bit set so the two spaces never collide. */
typedef enum
{
    /* commands: console -> esp */
    NP_CMD_PING = 0x01,        /* -> PONG                                        */
    NP_CMD_SCAN = 0x02,        /* -> SCAN_RESULT                                 */
    NP_CMD_CONNECT = 0x03,     /* {ssid_len,ssid,pass_len,pass} -> STATUS        */
    NP_CMD_STATUS = 0x04,      /* -> STATUS                                      */
    NP_CMD_HTTP_OPEN = 0x05,   /* {url bytes} -> HTTP_OPEN                        */
    NP_CMD_HTTP_READ = 0x06,   /* {max:u16} -> HTTP_DATA                          */
    NP_CMD_HTTP_CLOSE = 0x07,  /* -> OK                                          */
    NP_CMD_DISCONNECT = 0x08,  /* -> OK                                          */

    /* responses: esp -> console */
    NP_RSP_PONG = 0x81,
    NP_RSP_SCAN = 0x82,        /* {count:u8, count x {rssi:i8,enc:u8,len:u8,ssid}} */
    NP_RSP_STATUS = 0x83,      /* {state:u8, ip[4]}                              */
    NP_RSP_HTTP_OPEN = 0x85,   /* {http_status:u16, content_length:u32}          */
    NP_RSP_HTTP_DATA = 0x86,   /* {raw bytes}; empty payload = EOF               */
    NP_RSP_OK = 0x87,
    NP_RSP_LOG = 0x88,         /* {level:u8, message bytes} — diagnostic, sent    */
                               /* before the real response; console -> SWO        */
    NP_RSP_ERR = 0xEE,         /* {code:u8}                                      */
} NetworkFrameType;

/* Severity for NP_RSP_LOG; values match the console LoggerLevel enum order. */
#define NP_LOG_ERROR 0
#define NP_LOG_WARN 1
#define NP_LOG_INFO 2
#define NP_LOG_DEBUG 3

/* Link/WiFi state reported in a STATUS response. */
typedef enum
{
    NP_STATE_DISCONNECTED = 0,
    NP_STATE_CONNECTING = 1,
    NP_STATE_CONNECTED = 2,
    NP_STATE_FAILED = 3,
} NetworkState;

/* AP encryption flag in a SCAN entry. */
typedef enum
{
    NP_ENC_OPEN = 0,
    NP_ENC_SECURED = 1,
} NetworkEnc;

/* Sentinel content_length when the server sends no Content-Length header. */
#define NP_LENGTH_UNKNOWN 0xFFFFFFFFu

/*
 * CRC-16-CCITT used for per-frame integrity. Defined inline here so both sides
 * compute it from the exact same code (init 0xFFFF, poly 0x1021, MSB-first, no
 * final XOR — identical to the console's crc16_calculate()).
 */
static inline uint16_t np_crc16(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0u; i < length; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8u);
        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1u) ^ 0x1021u) : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

#endif /* NETWORK_PROTOCOL_H */
