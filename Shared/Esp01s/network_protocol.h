#ifndef NETWORK_PROTOCOL_H
#define NETWORK_PROTOCOL_H

/*
 * Console <-> ESP-01S serial protocol contract.
 *
 * Single source of truth shared by BOTH sides of the link:
 *   - the STM32 console firmware  (Console/Src/Network), and
 *   - the ESP-01S firmware        (Esp01s/, built with PlatformIO).
 *
 * It is intentionally tiny for now: the current milestone is just a blinky ESP
 * image that the console can flash. The command set below is a placeholder
 * skeleton for the future runtime link (the planned "pull games over WiFi"
 * path) so both sides already compile against one agreed contract.
 */

#include <stdint.h>

/* Runtime UART baud for the console<->ESP link on USART1.
 * NOTE: firmware *flashing* uses the ESP ROM bootloader baud (115200), which is
 * a separate, lower rate — see docu/flasher.md. This constant is the rate the
 * two run-time firmwares talk at once the ESP is up. */
#define NETWORK_UART_BAUD 921600u

/* Bumped whenever the wire format below changes incompatibly. */
#define NETWORK_PROTOCOL_VERSION 1u

/* Placeholder command set for the future runtime protocol. Blinky uses none of
 * these yet; they exist so the contract has a shape both sides share. */
typedef enum
{
    NET_CMD_PING = 0x01,   /* console -> esp : are you alive?          */
    NET_CMD_PONG = 0x02,   /* esp -> console : yes                     */
    NET_CMD_STATUS = 0x03, /* console -> esp : report link/wifi state  */
} NetworkCommand;

#endif /* NETWORK_PROTOCOL_H */
