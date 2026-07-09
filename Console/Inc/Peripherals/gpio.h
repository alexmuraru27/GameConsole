#ifndef __GPIO_H
#define __GPIO_H
#include <stdint.h>
#include <stdbool.h>

/* Configure every pin the console uses (once at boot). The runtime accessors that
 * used to live here now sit with their owning subsystems: sdCardPresent -> sdio.h,
 * button gather -> joystick.c, I2C bus recovery -> i2c.c, ESP-01 bootstrap -> esp01.h. */
void gpioInit(void);

#endif /* __GPIO_H */