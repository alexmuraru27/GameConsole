#ifndef __ESP_FLASHER_PORT_H
#define __ESP_FLASHER_PORT_H

#include "esp_loader_io.h"

/*
 * Bare-metal esp-serial-flasher port for this console. Implements the
 * esp_loader_port_ops_t vtable directly over the project's register-level
 * USART1 driver (usart.c) and the ESP-01 bootstrap pins (gpio.c) — the
 * upstream port/stm32_port.c depends on the STM32 HAL, which this firmware
 * does not use, so it is deliberately not compiled.
 *
 * Returns the port handle to hand to esp_loader_init_serial(). USART1 is
 * (re)initialised by the port's init callback.
 */
esp_loader_port_t *espFlasherPortGet(void);

#endif /* __ESP_FLASHER_PORT_H */
