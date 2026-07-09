#include "Devices/esp01.h"
#include "stm32f4xx.h"

/* ---- ESP-01 bootstrap pins (active-low logic, idle high) ----
 * EN=PB10, RST=PB6, IO0=PC6 (pins set up by gpioInit). */

void esp01SetEnable(bool enabled)
{
    GPIOB->BSRR = enabled ? GPIO_BSRR_BS10 : GPIO_BSRR_BR10; // PB10 (EN)
}

void esp01SetReset(bool in_reset)
{
    GPIOB->BSRR = in_reset ? GPIO_BSRR_BR6 : GPIO_BSRR_BS6; // PB6 (RST), low = reset
}

void esp01SetBootloader(bool enter)
{
    // PC6 (IO0): drive low to select the ROM bootloader; otherwise release to
    // input (Hi-Z) so the module's pull-up restores high and the running ESP
    // firmware owns GPIO0. Driving it high (push-pull) would fight the firmware.
    if (enter)
    {
        GPIOC->BSRR = GPIO_BSRR_BR6; // preset low before enabling the output
        GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODE6_Msk) | (1U << GPIO_MODER_MODE6_Pos);
    }
    else
    {
        GPIOC->MODER &= ~GPIO_MODER_MODE6_Msk; // back to input (Hi-Z)
    }
}
