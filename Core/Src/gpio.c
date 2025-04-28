#include "gpio.h"
#include <stm32f407xx.h>

static void initGpioUsart2()
{
    // Set PA2(TX) and PA3(RX) to Alternate Function
    GPIOA->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOA->MODER |= (2U << GPIO_MODER_MODER2_Pos) | (2U << GPIO_MODER_MODER3_Pos);

    // Set alternate function = 7 (USART2_TX/RX)
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL2_Msk | GPIO_AFRL_AFSEL3_Msk);
    GPIOA->AFR[0] |= (7U << GPIO_AFRL_AFSEL2_Pos) | (7U << GPIO_AFRL_AFSEL3_Pos);

    // Set High speed for the gpio pins
    GPIOA->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED2_Pos) | (3U << GPIO_OSPEEDR_OSPEED3_Pos);
}

static void initGpioLed1()
{
    // Set PA6 as output
    GPIOA->MODER &= ~GPIO_MODER_MODER6;
    GPIOA->MODER |= (0x1 << GPIO_MODER_MODER6_Pos);

    // Set PA6 output type as push-pull (default)
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT6;

    // Set PA6 speed to high (optional)
    GPIOA->OSPEEDR |= (0x3 << GPIO_OSPEEDR_OSPEED6_Pos);

    // Disable pull-up/pull-down for PA6
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD6;
}

void gpioInit(void)
{
    initGpioUsart2();
    initGpioLed1();
}