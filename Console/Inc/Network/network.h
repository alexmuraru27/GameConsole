#ifndef __NETWORK_H
#define __NETWORK_H

#include <stdbool.h>
#include "network_protocol.h"

/*
 * Console-side network API — the surface the console firmware uses to talk to
 * the ESP-01S over USART1 at runtime (NETWORK_UART_BAUD). The runtime link is
 * not implemented yet (network.c is a stub); these are the entry points it will
 * fill. The shared wire contract lives in Shared/Esp01s/network_protocol.h.
 *
 * Note: *flashing* new ESP firmware is a separate path — see
 * Console/Src/Flasher and docu/flasher.md — and does not go through this API.
 */

/* Bring up the runtime console<->ESP link (USART1 @ NETWORK_UART_BAUD). */
void networkInit(void);

/* True once a healthy link to the ESP has been established. */
bool networkIsConnected(void);

#endif /* __NETWORK_H */
