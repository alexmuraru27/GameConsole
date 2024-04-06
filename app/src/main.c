#include "stm32f407xx.h"
#include "clock/clock_config.h"

void gpio_config(void)
{
    GPIOA->MODER |= (1 << GPIO_MODER_MODER6_Pos);
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6;
    GPIOA->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED6_Pos);

    // switch_input
    GPIOA->MODER &= ~(GPIO_MODER_MODER2);
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD2);
    GPIOA->PUPDR |= (GPIO_PUPDR_PUPD2_0);

    // ADC 1 X axis
    GPIOA->MODER &= ~(GPIO_MODER_MODER0);

    // ADC 2 Y axis
    GPIOA->MODER &= ~(GPIO_MODER_MODER1);
}

void hw_init(void)
{
    system_clock_config();
    gpio_config();
}

uint8_t get_switch_input()
{
    return (GPIOA->IDR & GPIO_IDR_ID2) >> GPIO_IDR_ID2_Pos == 0U;
}

int main(void)
{
    __enable_irq();
    hw_init();

    while (1)
    {
        GPIOA->BSRR |= (1 << 6);
        delay_ms_systick(500U * (get_switch_input() == 0));
        GPIOA->BSRR |= (1 << 6) << 16;
        delay_ms_systick(500U);
    }
}