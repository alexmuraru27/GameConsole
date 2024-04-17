#include "clock/app_clock_config.h"
#include "shared/include/sys_clock_config/sys_clock_config.h"

static void peripherals_clock_enable(void)
{
    // ######## AHB1 ########
    // Pass clock to GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
}

void app_clock_config(void)
{
    system_clock_config();
    peripherals_clock_enable();
}