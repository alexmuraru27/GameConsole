#include "network.h"
#include "logger.h"

/*
 * Runtime console<->ESP-01S network link. Stub for now: the implemented
 * milestone is the ability to *flash* the ESP (Console/Src/Flasher,
 * docu/flasher.md). The runtime protocol over USART1 (NETWORK_UART_BAUD,
 * NetworkCommand) lands here later, when the console pulls content from the
 * ESP/PC over WiFi.
 */

void networkInit(void)
{
    LOGGER_LOG_INFO(LOGGER_NETWORK, "network init: runtime link not implemented yet");
}

bool networkIsConnected(void)
{
    return false;
}
