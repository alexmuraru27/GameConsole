#ifndef __DEVICES_ESP01_H
#define __DEVICES_ESP01_H

#include <stdbool.h>

/* ESP-01S bootstrap / control lines (active-low logic, idle high). The pins are
 * configured by gpioInit(); these drive them to power-cycle the module and select
 * its boot mode (run firmware vs. ROM download loader). Used by the network driver
 * and the ESP flasher — kept here rather than in gpio.c so the module's bootstrap
 * protocol lives with the thing it controls, not the generic pin setup. */
void esp01SetEnable(bool enabled);   /* EN/CH_PD: true = chip powered/enabled. */
void esp01SetReset(bool in_reset);   /* RST: true = held in reset (pin low). */
void esp01SetBootloader(bool enter); /* IO0: true = ROM bootloader select (pin low). */

#endif /* __DEVICES_ESP01_H */
