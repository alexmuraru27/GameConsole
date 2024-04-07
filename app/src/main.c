#include "stm32f407xx.h"
#include "clock/clock_config.h"
#include "joystick/joystick.h"

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
    joystick_init();
}

int main(void)
{
    __enable_irq();
    hw_init();
    while (1)
    {
        // adc_read();
        // OFF
        GPIOA->BSRR |= (1 << 6);
        delay_ms_systick(get_joystick_x() * (get_joystick_switch() == 0));
        // ON
        GPIOA->BSRR |= (1 << 6) << 16;
        delay_ms_systick(get_joystick_y());
    }
}