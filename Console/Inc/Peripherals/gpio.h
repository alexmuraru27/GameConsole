#ifndef __GPIO_H
#define __GPIO_H
#include <stdint.h>
#include <stdbool.h>
#include <stm32f407xx.h> /* GPIO_TypeDef for the pin accessors below */

/* Configure every pin the console uses (once at boot). The runtime accessors that
 * used to live here now sit with their owning subsystems: sdCardPresent -> sdio.h,
 * button gather -> joystick.c, I2C bus recovery -> i2c.c, ESP-01 bootstrap -> esp01.h. */
void gpioInit(void);

/* MODER field values (2 bits/pin), shared by the config table and gpioSetPinMode. */
#define GPIO_MODE_INPUT  0U
#define GPIO_MODE_OUTPUT 1U
#define GPIO_MODE_AF     2U
#define GPIO_MODE_ANALOG 3U

/* Named pin numbers (bit positions 0..15) for the board signals the drivers drive
 * or read at runtime. Pass the paired port shown in the comment to the accessors
 * below — e.g. gpioClearPin(GPIOC, GPIO_DISPLAY_RST). See docu/HW.md for the full
 * pinout; these are also used by the config tables in gpio.c so each pin number
 * has a single definition. */
#define GPIO_DISPLAY_RST 7U  /* PC7  — ILI9341 reset          */
#define GPIO_SD_DETECT   3U  /* PD3  — SD card detect (active-low) */
#define GPIO_ESP_EN      10U /* PB10 — ESP-01 CH_PD / enable  */
#define GPIO_ESP_RST     6U  /* PB6  — ESP-01 reset (low = reset) */
#define GPIO_ESP_IO0     6U  /* PC6  — ESP-01 GPIO0 boot strap */
#define GPIO_ESP_IO2     13U /* PC13 — ESP-01 GPIO2 strap      */
#define GPIO_I2C_SCL     8U  /* PB8  — I2C1 SCL               */
#define GPIO_I2C_SDA     9U  /* PB9  — I2C1 SDA               */

/* ------------------------------------------------------------------ *
 *  Single-pin digital I/O over the pins gpioInit() already configured,
 *  so a driver expresses a signal as port + pin (+ level) instead of
 *  hand-writing BSRR / IDR / MODER bit masks. `pin` is 0..15. The pin's
 *  direction/AF/pull come from gpioInit; these only drive or read it
 *  (gpioSetPinMode is the one exception — the I2C-recovery / ESP-boot
 *  sequences that must briefly retarget a pin between GPIO and AF).
 * ------------------------------------------------------------------ */
void gpioSetPin(GPIO_TypeDef *port, uint8_t pin);              /* drive high (BSRR set) */
void gpioClearPin(GPIO_TypeDef *port, uint8_t pin);            /* drive low  (BSRR reset) */
void gpioWritePin(GPIO_TypeDef *port, uint8_t pin, bool level);
bool gpioReadPin(GPIO_TypeDef *port, uint8_t pin);             /* live input level (IDR) */
void gpioSetPinMode(GPIO_TypeDef *port, uint8_t pin, uint8_t mode); /* GPIO_MODE_* */

#endif /* __GPIO_H */
