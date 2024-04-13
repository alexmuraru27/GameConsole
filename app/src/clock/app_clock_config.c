#include "clock/app_clock_config.h"
#include "shared/include/sys_clock_config/sys_clock_config.h"

static void peripherals_clock_enable(void)
{
    // ######## AHB1 ########
    // Pass clock to GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Pass clock to GPIOB
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // Pass clock to GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // Pass clock to DMA controllers
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    // ######## APB1 ########
    // Enable clock for UART4
    RCC->APB1ENR |= RCC_APB1ENR_UART4EN;

    // Enable clock for SPI1
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    // ######## APB2 ########
    // Enable clock for ADC1
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
}

void app_clock_config(void)
{
    system_clock_config();
    peripherals_clock_enable();
}