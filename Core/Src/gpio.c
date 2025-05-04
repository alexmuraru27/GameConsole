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

static void initGpioSpi1()
{
    // 1. PA5 (SCK - AF5)
    // 2. PA6 (MISO - AF5)
    // 3. PA7 (MOSI - AF5)
    // 4. PC4 (DC - Normal GPIO AF)
    // 5. PC5 (RST - Normal GPIO AF)
    // 5. PC6 (CS - Normal GPIO AF)

    // ############### SPI PERIHPERAL ###################
    //  Set GPIO to AF
    GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOA->MODER |= (2U << GPIO_MODER_MODER5_Pos) | (2U << GPIO_MODER_MODER6_Pos) | (2U << GPIO_MODER_MODER7_Pos);

    //  Set alternate function = 5 (SPI1_SCK/SPI1_MISO/SPI1_MOSI)
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL5_Msk | GPIO_AFRL_AFSEL6_Msk | GPIO_AFRL_AFSEL7_Msk);
    GPIOA->AFR[0] |= (5U << GPIO_AFRL_AFSEL5_Pos) | (5U << GPIO_AFRL_AFSEL6_Pos) | (5U << GPIO_AFRL_AFSEL7_Pos);

    // Set High speed for the pins
    GPIOA->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED5_Pos) | (3U << GPIO_OSPEEDR_OSPEED6_Pos) | (3U << GPIO_OSPEEDR_OSPEED7_Pos);

    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD5 | GPIO_PUPDR_PUPD6 | GPIO_PUPDR_PUPD7);

    // ############### SPI additional ###################
    // Set PC4(DC) and PC5(RST) PC6(CD) to Output
    GPIOC->MODER &= ~(GPIO_MODER_MODER4 | GPIO_MODER_MODER5 | GPIO_MODER_MODER6);
    GPIOC->MODER |= (1U << GPIO_MODER_MODER4_Pos) | (1U << GPIO_MODER_MODER5_Pos) | (1U << GPIO_MODER_MODER6_Pos);

    // Set PB12/PB11 output type as push-pull (default)
    GPIOC->OTYPER &= ~(GPIO_OTYPER_OT4 | GPIO_OTYPER_OT5 | GPIO_OTYPER_OT6);

    // Set High speed for the gpio pins
    GPIOC->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED4_Pos) | (3U << GPIO_OSPEEDR_OSPEED5_Pos) | (3U << GPIO_OSPEEDR_OSPEED6_Pos);

    // Disable pull-up/pull-down for PB12/PB11
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPD4 | GPIO_PUPDR_PUPD5 | GPIO_PUPDR_PUPD6);
    GPIOC->PUPDR |= (1U << GPIO_PUPDR_PUPD5_Pos);
}

void gpioSpi1RstLow(void)
{
    GPIOC->BSRR = GPIO_BSRR_BR5;
}

void gpioSpi1RstHigh(void)
{
    GPIOC->BSRR = GPIO_BSRR_BS5;
}

void gpioSpi1DcLow(void)
{
    GPIOC->BSRR = GPIO_BSRR_BR4;
}

void gpioSpi1DcHigh(void)
{
    GPIOC->BSRR = GPIO_BSRR_BS4;
}

void gpioInit(void)
{
    initGpioUsart2();
    initGpioSpi1();
}