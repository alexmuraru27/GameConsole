#include "gpio.h"
#include <stm32f407xx.h>

static void initGpioUsart3()
{
    // TODO initGpioUsart3
}

static void initGpioJoystick()
{
    // PA0  (Right D-Pad UP)
    // PA1  (Right D-Pad RIGHT)
    // PA5  (Right D-Pad DOWN)
    // PA6  (Right D-Pad LEFT)
    // PA7  (Left D-Pad UP)
    // PA8  (Left D-Pad RIGHT)
    // PA11 (Left D-Pad DOWN)
    // PA12 (Left D-Pad LEFT)
    // PB11 (Special Button 1)
    // PB12 (Special Button 2)

    // Input mode (clear MODER bits)
    GPIOA->MODER &= ~(GPIO_MODER_MODE0_Msk | GPIO_MODER_MODE1_Msk | GPIO_MODER_MODE5_Msk |
                      GPIO_MODER_MODE6_Msk | GPIO_MODER_MODE7_Msk | GPIO_MODER_MODE8_Msk |
                      GPIO_MODER_MODE11_Msk | GPIO_MODER_MODE12_Msk);
    GPIOB->MODER &= ~(GPIO_MODER_MODE11_Msk | GPIO_MODER_MODE12_Msk);

    // Pull-up
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1 | GPIO_PUPDR_PUPD5 |
                      GPIO_PUPDR_PUPD6 | GPIO_PUPDR_PUPD7 | GPIO_PUPDR_PUPD8 |
                      GPIO_PUPDR_PUPD11 | GPIO_PUPDR_PUPD12);
    GPIOA->PUPDR |= (1U << GPIO_PUPDR_PUPD0_Pos) | (1U << GPIO_PUPDR_PUPD1_Pos) |
                    (1U << GPIO_PUPDR_PUPD5_Pos) | (1U << GPIO_PUPDR_PUPD6_Pos) |
                    (1U << GPIO_PUPDR_PUPD7_Pos) | (1U << GPIO_PUPDR_PUPD8_Pos) |
                    (1U << GPIO_PUPDR_PUPD11_Pos) | (1U << GPIO_PUPDR_PUPD12_Pos);
    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPD11 | GPIO_PUPDR_PUPD12);
    GPIOB->PUPDR |= (1U << GPIO_PUPDR_PUPD11_Pos) | (1U << GPIO_PUPDR_PUPD12_Pos);
}

static void initGpioFsmc(void)
{
    // FSMC 16-bit 8080 parallel interface for ILI9341:
    // Data:    PD14(D0) PD15(D1) PD0(D2) PD1(D3) PE7(D4) PE8(D5) PE9(D6) PE10(D7)
    //          PE11(D8) PE12(D9) PE13(D10) PE14(D11) PE15(D12) PD8(D13) PD9(D14) PD10(D15)
    // Control: PD4(NOE/RD) PD5(NWE/WR) PD7(NE1/CS) PD11(A16/DC)  — all AF12
    // RST:     PC7 — GPIO output
    // BL:      PA3 — GPIO output (high = on)

    // ---- Port D: PD0,1,4,5,7,8,9,10,11,14,15 -> AF12 ----
    GPIOD->MODER &= ~(GPIO_MODER_MODE0_Msk | GPIO_MODER_MODE1_Msk |
                      GPIO_MODER_MODE4_Msk | GPIO_MODER_MODE5_Msk | GPIO_MODER_MODE7_Msk |
                      GPIO_MODER_MODE8_Msk | GPIO_MODER_MODE9_Msk | GPIO_MODER_MODE10_Msk |
                      GPIO_MODER_MODE11_Msk | GPIO_MODER_MODE14_Msk | GPIO_MODER_MODE15_Msk);
    GPIOD->MODER |= (2U << GPIO_MODER_MODE0_Pos) | (2U << GPIO_MODER_MODE1_Pos) |
                    (2U << GPIO_MODER_MODE4_Pos) | (2U << GPIO_MODER_MODE5_Pos) |
                    (2U << GPIO_MODER_MODE7_Pos) | (2U << GPIO_MODER_MODE8_Pos) |
                    (2U << GPIO_MODER_MODE9_Pos) | (2U << GPIO_MODER_MODE10_Pos) |
                    (2U << GPIO_MODER_MODE11_Pos) | (2U << GPIO_MODER_MODE14_Pos) |
                    (2U << GPIO_MODER_MODE15_Pos);
    GPIOD->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED0_Pos) | (3U << GPIO_OSPEEDR_OSPEED1_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED4_Pos) | (3U << GPIO_OSPEEDR_OSPEED5_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED7_Pos) | (3U << GPIO_OSPEEDR_OSPEED8_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED9_Pos) | (3U << GPIO_OSPEEDR_OSPEED10_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED11_Pos) | (3U << GPIO_OSPEEDR_OSPEED14_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED15_Pos);
    // AFRL: PD0, PD1, PD4, PD5, PD7
    GPIOD->AFR[0] &= ~(GPIO_AFRL_AFSEL0_Msk | GPIO_AFRL_AFSEL1_Msk |
                       GPIO_AFRL_AFSEL4_Msk | GPIO_AFRL_AFSEL5_Msk | GPIO_AFRL_AFSEL7_Msk);
    GPIOD->AFR[0] |= (12U << GPIO_AFRL_AFSEL0_Pos) | (12U << GPIO_AFRL_AFSEL1_Pos) |
                     (12U << GPIO_AFRL_AFSEL4_Pos) | (12U << GPIO_AFRL_AFSEL5_Pos) |
                     (12U << GPIO_AFRL_AFSEL7_Pos);
    // AFRH: PD8, PD9, PD10, PD11, PD14, PD15
    GPIOD->AFR[1] &= ~(GPIO_AFRH_AFSEL8_Msk | GPIO_AFRH_AFSEL9_Msk |
                       GPIO_AFRH_AFSEL10_Msk | GPIO_AFRH_AFSEL11_Msk |
                       GPIO_AFRH_AFSEL14_Msk | GPIO_AFRH_AFSEL15_Msk);
    GPIOD->AFR[1] |= (12U << GPIO_AFRH_AFSEL8_Pos) | (12U << GPIO_AFRH_AFSEL9_Pos) |
                     (12U << GPIO_AFRH_AFSEL10_Pos) | (12U << GPIO_AFRH_AFSEL11_Pos) |
                     (12U << GPIO_AFRH_AFSEL14_Pos) | (12U << GPIO_AFRH_AFSEL15_Pos);

    // ---- Port E: PE7-PE15 -> AF12 ----
    GPIOE->MODER &= ~(GPIO_MODER_MODE7_Msk | GPIO_MODER_MODE8_Msk | GPIO_MODER_MODE9_Msk |
                      GPIO_MODER_MODE10_Msk | GPIO_MODER_MODE11_Msk | GPIO_MODER_MODE12_Msk |
                      GPIO_MODER_MODE13_Msk | GPIO_MODER_MODE14_Msk | GPIO_MODER_MODE15_Msk);
    GPIOE->MODER |= (2U << GPIO_MODER_MODE7_Pos) | (2U << GPIO_MODER_MODE8_Pos) |
                    (2U << GPIO_MODER_MODE9_Pos) | (2U << GPIO_MODER_MODE10_Pos) |
                    (2U << GPIO_MODER_MODE11_Pos) | (2U << GPIO_MODER_MODE12_Pos) |
                    (2U << GPIO_MODER_MODE13_Pos) | (2U << GPIO_MODER_MODE14_Pos) |
                    (2U << GPIO_MODER_MODE15_Pos);
    GPIOE->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED7_Pos) | (3U << GPIO_OSPEEDR_OSPEED8_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED9_Pos) | (3U << GPIO_OSPEEDR_OSPEED10_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED11_Pos) | (3U << GPIO_OSPEEDR_OSPEED12_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED13_Pos) | (3U << GPIO_OSPEEDR_OSPEED14_Pos) |
                      (3U << GPIO_OSPEEDR_OSPEED15_Pos);
    // AFRL: PE7
    GPIOE->AFR[0] &= ~GPIO_AFRL_AFSEL7_Msk;
    GPIOE->AFR[0] |= (12U << GPIO_AFRL_AFSEL7_Pos);
    // AFRH: PE8-PE15
    GPIOE->AFR[1] &= ~(GPIO_AFRH_AFSEL8_Msk | GPIO_AFRH_AFSEL9_Msk |
                       GPIO_AFRH_AFSEL10_Msk | GPIO_AFRH_AFSEL11_Msk |
                       GPIO_AFRH_AFSEL12_Msk | GPIO_AFRH_AFSEL13_Msk |
                       GPIO_AFRH_AFSEL14_Msk | GPIO_AFRH_AFSEL15_Msk);
    GPIOE->AFR[1] |= (12U << GPIO_AFRH_AFSEL8_Pos) | (12U << GPIO_AFRH_AFSEL9_Pos) |
                     (12U << GPIO_AFRH_AFSEL10_Pos) | (12U << GPIO_AFRH_AFSEL11_Pos) |
                     (12U << GPIO_AFRH_AFSEL12_Pos) | (12U << GPIO_AFRH_AFSEL13_Pos) |
                     (12U << GPIO_AFRH_AFSEL14_Pos) | (12U << GPIO_AFRH_AFSEL15_Pos);

    // ---- PC7: RST -> push-pull output, initially high ----
    GPIOC->MODER &= ~GPIO_MODER_MODE7_Msk;
    GPIOC->MODER |= (1U << GPIO_MODER_MODE7_Pos);
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT7;
    GPIOC->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED7_Pos);
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD7;
    GPIOC->BSRR = GPIO_BSRR_BS7;

    // ---- PA3: BL -> push-pull output, high (backlight on) ----
    GPIOA->MODER &= ~GPIO_MODER_MODE3_Msk;
    GPIOA->MODER |= (1U << GPIO_MODER_MODE3_Pos);
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT3;
    GPIOA->OSPEEDR |= (3U << GPIO_OSPEEDR_OSPEED3_Pos);
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD3;
    GPIOA->BSRR = GPIO_BSRR_BS3;
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
    // PD3: SD card detect — input with pull-up (active-low: 0 = card present)
    GPIOD->MODER &= ~GPIO_MODER_MODE3_Msk;
    GPIOD->PUPDR &= ~GPIO_PUPDR_PUPD3_Msk;
    GPIOD->PUPDR |= (1U << GPIO_PUPDR_PUPD3_Pos);

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
    GPIOD->PUPDR |= (1 << 4); // Pull-up
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

bool sdCardPresent(void)
{
    return (GPIOD->IDR & GPIO_IDR_ID3_Msk) == 0U;
}

void gpioInit(void)
{
    initGpioUsart3();
    initGpioJoystick();
    initGpioAdc1();
    initGpioBuzzer();
    initSdio();
    initI2C1();
    initGpioFsmc();
}
