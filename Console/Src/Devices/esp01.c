#include "Devices/esp01.h"
#include "Peripherals/gpio.h"

/* ---- ESP-01 bootstrap pins (active-low logic, idle high) ----
 * EN=PB10, RST=PB6, IO0=PC6 (pins set up by gpioInit). */

void esp01SetEnable(bool enabled)
{
    gpioWritePin(GPIOB, GPIO_ESP_EN, enabled);
}

void esp01SetReset(bool in_reset)
{
    gpioWritePin(GPIOB, GPIO_ESP_RST, !in_reset); // low = reset
}

void esp01SetBootloader(bool enter)
{
    // IO0: drive low to select the ROM bootloader; otherwise release to input
    // (Hi-Z) so the module's pull-up restores high and the running ESP firmware
    // owns GPIO0. Driving it high (push-pull) would fight the firmware.
    if (enter)
    {
        gpioClearPin(GPIOC, GPIO_ESP_IO0); // preset low before enabling the output
        gpioSetPinMode(GPIOC, GPIO_ESP_IO0, GPIO_MODE_OUTPUT);
    }
    else
    {
        gpioSetPinMode(GPIOC, GPIO_ESP_IO0, GPIO_MODE_INPUT); // back to input (Hi-Z)
    }
}
