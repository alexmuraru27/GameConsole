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

static void initGpioUsart3()
{
    // TODO initGpioUsart3
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

static void initI2C1()
{
    // 1. PB8 (I2C1_SCL - AF4)
    // 2. PB9 (I2C2_SDA - AF4)

    // Set AF mode
    GPIOB->MODER &= ~(GPIO_MODER_MODE8_Msk | GPIO_MODER_MODE9_Msk);
    GPIOB->MODER |= (2U << GPIO_MODER_MODE8_Pos) | (2U << GPIO_MODER_MODE9_Pos);

    // Very high speed
    GPIOB->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED8_Pos) | (3U << GPIO_OSPEEDR_OSPEED9_Pos);

    // AF4
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFRH0 | GPIO_AFRH_AFRH1);
    GPIOB->AFR[1] |= ((4U << GPIO_AFRH_AFSEL8_Pos) | (4U << GPIO_AFRH_AFSEL9_Pos));

    // Open drain
    GPIOB->OTYPER |= (1U << GPIO_OTYPER_OT8_Pos) | (1U << GPIO_OTYPER_OT9_Pos);

    // Pullups
    GPIOB->PUPDR |= (1U << GPIO_PUPDR_PUPD8_Pos) | (1U << GPIO_PUPDR_PUPD9_Pos);
}

void gpioInit(void)
{
    initGpioUsart2();
    initGpioUsart3();
    initGpioJoystick();
    initGpioAdc1();
    initGpioBuzzer();
    initSdio();
    initI2C1();
}
