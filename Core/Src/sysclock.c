#include "sysclock.h"
#include "stm32f407xx.h"

static volatile uint32_t timing_delay = 0U;
static volatile uint32_t system_time = 0U;

#if !defined(HSE_CLOCK_VALUE)
#define HSE_CLOCK_VALUE ((uint32_t)8000000) /*!< Default value of the External oscillator in Hz */
#endif                                      /* HSE_CLOCK_VALUE */

#if !defined(HSI_CLOCK_VALUE)
#define HSI_CLOCK_VALUE ((uint32_t)16000000) /*!< Value of the Internal oscillator in Hz*/
#endif                                       /* HSI_CLOCK_VALUE */

void sysTickClockUpdate(void)
{
    uint32_t systemCoreClock = HSI_CLOCK_VALUE;
    uint32_t tmp = 0, pllvco = 0, pllp = 2, pllsource = 0, pllm = 2;

    /* Get SYSCLK source -------------------------------------------------------*/
    tmp = RCC->CFGR & RCC_CFGR_SWS;

    switch (tmp)
    {
    case RCC_CFGR_SWS_HSI: /* HSI used as system clock source */
        systemCoreClock = HSI_CLOCK_VALUE;
        break;
    case RCC_CFGR_SWS_HSE: /* HSE used as system clock source */
        systemCoreClock = HSE_CLOCK_VALUE;
        break;
    case RCC_CFGR_SWS_PLL: /* PLL used as system clock source */
        /* PLL_VCO = (HSE_CLOCK_VALUE or HSI_CLOCK_VALUE / PLL_M) * PLL_N
           SYSCLK = PLL_VCO / PLL_P
           */
        pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) >> 22;
        pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;

        if (pllsource != RCC_PLLCFGR_PLLSRC_HSI)
        {
            /* HSE used as PLL clock source */
            pllvco = (HSE_CLOCK_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        }
        else
        {
            /* HSI used as PLL clock source */
            pllvco = (HSI_CLOCK_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        }

        pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;
        systemCoreClock = pllvco / pllp;
        break;
    default:
        break;
    }

    /* Compute HCLK frequency --------------------------------------------------*/
    /* Get HCLK prescaler */
    const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
    tmp = AHBPrescTable[((RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos)];
    /* HCLK frequency */
    systemCoreClock >>= tmp;

    // Divide sec by 1000 to get SysTick at 1ms
    SysTick_Config(systemCoreClock / 1000);
}

// Interrupt handler
void SysTick_Handler(void)
{
    system_time++;
}

void delayMs(uint32_t time)
{
    timing_delay = system_time + time;
    while (timing_delay > system_time)
    {
    };
}

uint32_t getSysTime()
{
    return system_time;
}

static void selectClockSourceConfig(void)
{
    // No bypass, using internal clock
    RCC->CR &= ~RCC_CR_HSEBYP;

    // Enable HSE
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
    {
    }; // Wait until HSE is ready
}

static void flashMemoryClockConfig(void)
{
    FLASH->ACR |= FLASH_ACR_PRFTEN;
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_5WS;
}

static void busClockConfig(void)
{
    // AHB Prescaler = 1
    RCC->CFGR &= ~RCC_CFGR_HPRE;

    // APB1 - LOWSPEED = AHB SPEED/4
    RCC->CFGR &= ~RCC_CFGR_PPRE1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;

    // APB2 - HIGHSPEED = AHB SPEED/2
    RCC->CFGR &= ~RCC_CFGR_PPRE2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;
}

static void pllClockConfig(void)
{
    // SET source of PLL to HSE
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLSRC;
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;

    // PLLM input DIVIDER to 4U -> bring it down to 2MHz for stability
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM;
    RCC->PLLCFGR |= (4U << RCC_PLLCFGR_PLLM_Pos);

    // PLLN MULTIPLIER to 168
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN;
    RCC->PLLCFGR |= (168 << RCC_PLLCFGR_PLLN_Pos);

    // PLLP output DIVIDER = 2
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLP;

    // PLLQ DIVIDER = 4 > 42 MHZ -  USB OTG FS, SDIO and random number generator
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLQ;
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLQ_2;

    // Enable PLL - 168MHz
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
    {
    };

    // Switch RCC source to PLL
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    while (!(RCC->CFGR & RCC_CFGR_SWS))
    {
    };
}

static void peripheralsClockEnable(void)
{
    // ######## AHB1 ########
    // Pass clock to GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    // Pass clock to GPIOB
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    // Pass clock to GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    // Pass clock to GPIOD
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    // Pass clock to GPIOE
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;

    // TODO Pass clock to CCMRam
    // RCC->AHB1ENR |= RCC_AHB1ENR_CCMDATARAMEN;

    // ######## AHB2 ########
    // Pass clock to RNGEN
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;

    // ######## APB1 ########
    // TODO Enable SPI
    // TODO Enable UART
    // TODO Enable Timers

    // ######## APB2 ########
    // TODO Enable ADC
}

void systemClockConfig(void)
{
    selectClockSourceConfig();
    flashMemoryClockConfig();
    busClockConfig();
    pllClockConfig();
    sysTickClockUpdate();
    peripheralsClockEnable();
}