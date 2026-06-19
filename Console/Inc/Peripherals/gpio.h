#ifndef __GPIO_H
#define __GPIO_H
#include <stdint.h>
#include <stdbool.h>

void gpioInit(void);
bool sdCardPresent(void);
uint16_t gpioReadButtons(void);

/* ESP-01 bootstrap control (pins owned by gpio.c). */
void esp01SetEnable(bool enabled);   /* EN/CH_PD: true = chip powered/enabled. */
void esp01SetReset(bool in_reset);   /* RST: true = held in reset (pin low). */
void esp01SetBootloader(bool enter); /* IO0: true = ROM bootloader select (pin low). */
#endif /* __GPIO_H */