#ifndef __NETWORK_H
#define __NETWORK_H

#include <stdbool.h>
#include <stdint.h>
#include "network_protocol.h"

/*
 * Console-side driver for the ESP-01S WiFi link. The console is the master of
 * the framed UART protocol in network_protocol.h: each call sends one command
 * and blocks for the single response. Used by the download engine (downloader.c)
 * and the WiFi settings UI (wifi_menu.c).
 *
 * Note: *flashing* the ESP is a separate path (Console/Src/Flasher) and does not
 * go through this driver. The flasher leaves USART1 at 115200; these calls
 * re-assert the runtime baud (NETWORK_UART_BAUD) before transacting.
 */

typedef struct
{
    char ssid[NP_SSID_MAX + 1U]; /* NUL-terminated */
    int8_t rssi;                 /* dBm */
    uint8_t enc;                 /* NetworkEnc: 0 = open */
} NetworkAp;

/* Bring up USART1 at the runtime baud. Does not connect (connection is on
 * demand, using saved credentials, so boot isn't blocked). */
void networkInit(void);

/* Power-cycle the ESP via EN/CH_PD and re-assert the runtime baud, so it boots
 * fresh. Used at startup and after flashing new ESP firmware. */
void networkRebootEsp(void);

/* Round-trip ping — true if the ESP firmware answers. */
bool networkPing(void);

/* Handshake: ping the ESP (retrying to cover its post-reset boot) and check it
 * runs a matching protocol version. Logs the outcome; true if synced. Use it to
 * fail fast/clearly instead of waiting out a command timeout. */
bool networkSync(void);

/* Scan for access points. Fills up to `max` entries, returns the count or -1. */
int networkScan(NetworkAp *out, int max);

/* Associate with `ssid`/`pass` (blocking, ~up to 6 s on the ESP). True on success. */
bool networkConnect(const char *ssid, const char *pass);

/* Drop the association. */
void networkDisconnect(void);

/* Query the ESP's current link state. */
bool networkIsConnected(void);

/* Begin an HTTP GET of `url`. On success fills *content_length (may be
 * NP_LENGTH_UNKNOWN) and *http_status, and a read session is open until
 * networkHttpClose(). Returns false on transport/HTTP error. */
bool networkHttpOpen(const char *url, uint32_t *content_length, uint16_t *http_status);

/* Pull up to `max` body bytes into `buf`. Returns bytes read (0 = EOF) or -1. */
int networkHttpRead(uint8_t *buf, uint16_t max);

/* End the current GET session. */
void networkHttpClose(void);

#endif /* __NETWORK_H */
