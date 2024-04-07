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
    JOYSTICK_VERTICAL y = get_joystick_y();
    JOYSTICK_HORIZONTAL x = get_joystick_x();
    JOYSTICK_SWITCH sw = get_joystick_switch();
    while (1)
    {
        // adc_read();
        // OFF
        y = get_joystick_y();
        x = get_joystick_x();
        sw = get_joystick_switch();
        GPIOA->BSRR |= (1 << 6);
        delay_ms_systick(100 * x * (sw == JOYSTICK_SWITCH_OFF));
        // ON
        GPIOA->BSRR |= (1 << 6) << 16;
        delay_ms_systick(100 * y);
    }
}