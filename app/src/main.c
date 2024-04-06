#include "stm32f407xx.h"
#include "clock/clock_config.h"

void gpio_config(void)
{
    GPIOA->MODER |= (1 << GPIO_MODER_MODER6_Pos);
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6;
    GPIOA->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED6_Pos);
}

void hw_init(void)
{
    system_clock_config();
    gpio_config();
}

int main(void)
{
    __enable_irq();
    hw_init();

    while (1)
    {
        GPIOA->BSRR |= (1 << 6);
        delay_ms_systick(500U);
        GPIOA->BSRR |= (1 << 6) << 16;
        delay_ms_systick(500U);
    }
}