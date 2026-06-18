#include "usart.h"
#include <stdbool.h>
#include <stm32f407xx.h>
#include <string.h>
#include "logger.h"

void usartInit(void)
{
    /* USART1 (ESP-01) is not yet brought up — the network stack is a stub. */
    LOGGER_LOG_DEBUG(LOGGER_CORE, "usart init: no-op (network not implemented)");
}