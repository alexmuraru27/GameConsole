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
    //  PA9 (DC - Normal GPIO AF)  PC7 (RST - Normal GPIO AF) PB6 (CS - Normal GPIO AF) to Output
    GPIOA->MODER &= ~(GPIO_MODER_MODER9);
    GPIOA->MODER |= (1U << GPIO_MODER_MODER9_Pos);
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT9);
    GPIOA->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED9);
    GPIOA->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED9_Pos);
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD9);

    GPIOC->MODER &= ~(GPIO_MODER_MODER7);
    GPIOC->MODER |= (1U << GPIO_MODER_MODER7_Pos);
    GPIOC->OTYPER &= ~(GPIO_OTYPER_OT7);
    GPIOC->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED7);
    GPIOC->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED7_Pos);
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPD7);

    GPIOB->MODER &= ~(GPIO_MODER_MODER6);
    GPIOB->MODER |= (1U << GPIO_MODER_MODER6_Pos);
    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT6);
    GPIOB->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED6);
    GPIOB->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED6_Pos);
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD6);

    gpioSpi1DcLow();
    gpioSpi1RstHigh();
    gpioSpi1CsHigh();
}

void gpioSpi1DcLow(void)
{
    GPIOA->BSRR = GPIO_BSRR_BR_9;
}

void gpioSpi1DcHigh(void)
{
    GPIOA->BSRR = GPIO_BSRR_BS_9;
}

void gpioSpi1RstLow(void)
{
    GPIOC->BSRR = GPIO_BSRR_BR_7;
}

void gpioSpi1RstHigh(void)
{
    GPIOC->BSRR = GPIO_BSRR_BS_7;
}

void gpioSpi1CsLow(void)
{
    GPIOB->BSRR = GPIO_BSRR_BR_6;
}

void gpioSpi1CsHigh(void)
{
    GPIOB->BSRR = GPIO_BSRR_BS_6;
}

static void initGpioJoystick()
{
    // PE7 (Right D-Pad UP)
    // PE8 (Right D-Pad RIGHT)
    // PE9 (Right D-Pad DOWN)
    // PE10 (Right D-Pad LEFT)
    // PE11 (Left D-Pad UP)
    // PE12 (Left D-Pad RIGHT)
    // PE13 (Left D-Pad DOWN)
    // PE14 (Left D-Pad LEFT)
    // PB11 (Special Button 1)
    // PB12 (Special Button 2)

    // Make pin inputs
    GPIOE->MODER &= ~(GPIO_MODER_MODER7 | GPIO_MODER_MODER8 | GPIO_MODER_MODER9 | GPIO_MODER_MODER10 | GPIO_MODER_MODER11 | GPIO_MODER_MODER12 | GPIO_MODER_MODER13 | GPIO_MODER_MODER14);
    GPIOB->MODER &= ~(GPIO_MODER_MODER11 | GPIO_MODER_MODER12);

    // Make pullup
    GPIOE->PUPDR &= ~(GPIO_PUPDR_PUPD7 | GPIO_PUPDR_PUPD8 | GPIO_PUPDR_PUPD9 | GPIO_PUPDR_PUPD10 | GPIO_PUPDR_PUPD11 | GPIO_PUPDR_PUPD12 | GPIO_PUPDR_PUPD13 | GPIO_PUPDR_PUPD14);
    GPIOE->PUPDR |= (1U << GPIO_PUPDR_PUPD7_Pos) | (1U << GPIO_PUPDR_PUPD8_Pos) | (1U << GPIO_PUPDR_PUPD9_Pos) | (1U << GPIO_PUPDR_PUPD10_Pos) | (1U << GPIO_PUPDR_PUPD11_Pos) | (1U << GPIO_PUPDR_PUPD12_Pos) | (1U << GPIO_PUPDR_PUPD13_Pos) | (1U << GPIO_PUPDR_PUPD14_Pos);
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD11 | GPIO_PUPDR_PUPD12);
    GPIOB->PUPDR |= (1U << GPIO_PUPDR_PUPD11_Pos) | (1U << GPIO_PUPDR_PUPD12_Pos);
}

static void initGpioAdc1()
{
    // configure PC0-PC3 as analog
    GPIOC->MODER |= (GPIO_MODER_MODER0 | GPIO_MODER_MODER1 | GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1 | GPIO_PUPDR_PUPD2 | GPIO_PUPDR_PUPD3);
}

static void initGpioBuzzer()
{
    // PB5
    // AF2
    GPIOB->MODER &= ~(GPIO_MODER_MODER5);
    GPIOB->MODER |= GPIO_MODER_MODER5_1;
    GPIOB->AFR[0] &= ~(GPIO_AFRL_AFSEL5_Msk);
    GPIOB->AFR[0] |= (2U << GPIO_AFRL_AFSEL5_Pos);
}

static void initSdio()
{

    // set to Alternate function mode
    GPIOC->MODER &= ~(GPIO_MODER_MODE8_Msk | GPIO_MODER_MODE9_Msk | GPIO_MODER_MODE10_Msk | GPIO_MODER_MODE11_Msk | GPIO_MODER_MODE12_Msk);

    GPIOC->MODER |= ((2U << GPIO_MODER_MODE8_Pos) | (2U << GPIO_MODER_MODE9_Pos) | (2U << GPIO_MODER_MODE10_Pos) |
                     (2U << GPIO_MODER_MODE11_Pos) | (2U << GPIO_MODER_MODE12_Pos));

    GPIOD->MODER &= ~(GPIO_MODER_MODE2_Msk);
    GPIOD->MODER |= (2U << GPIO_MODER_MODE2_Pos);

    // set speed to very high
    GPIOC->OSPEEDR |= ((3U << GPIO_OSPEEDR_OSPEED8_Pos) | (3U << GPIO_OSPEEDR_OSPEED9_Pos) | (3U << GPIO_OSPEEDR_OSPEED10_Pos) |
                       (3U << GPIO_OSPEEDR_OSPEED11_Pos) | (3U << GPIO_OSPEEDR_OSPEED12_Pos));
    GPIOD->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED2_Pos);

    // GPIOC PC8 PC9 PC10 PC11 PC12 - AF12
    GPIOC->AFR[1] &= ~(GPIO_AFRH_AFRH0 | GPIO_AFRH_AFRH1 | GPIO_AFRH_AFRH2 | GPIO_AFRH_AFRH3 | GPIO_AFRH_AFRH4);
    GPIOC->AFR[1] |= ((12U << GPIO_AFRH_AFSEL8_Pos) | (12U << GPIO_AFRH_AFSEL9_Pos) |
                      (12U << GPIO_AFRH_AFSEL10_Pos) | (12U << GPIO_AFRH_AFSEL11_Pos) |
                      (12U << GPIO_AFRH_AFSEL12_Pos));

    // GPIOD PD2 AF12
    GPIOD->AFR[0] &= ~(GPIO_AFRL_AFRL2);
    GPIOD->AFR[0] |= (12 << GPIO_AFRL_AFSEL2_Pos);

    // Pull-up for data lines (NOT CLK)
    GPIOC->PUPDR |= (1 << GPIO_PUPDR_PUPD8_Pos) | (1 << GPIO_PUPDR_PUPD9_Pos) | (1 << GPIO_PUPDR_PUPD10_Pos) | (1 << GPIO_PUPDR_PUPD11_Pos);
    // Configure PD2 (CMD) with pull-up
    GPIOD->PUPDR |= (1 << 4); // Pull-up (CRITICAL for Black Board)
}

void gpioInit(void)
{
    initGpioUsart2();
    initGpioSpi1();
    initGpioJoystick();
    initGpioAdc1();
    initGpioBuzzer();
    initSdio();
}
