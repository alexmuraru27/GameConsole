#include "sysclock.h"
#include "stdbool.h"
#include "stm32f407xx.h"

static volatile uint32_t s_timing_delay = 0U;
static volatile uint32_t s_system_time = 0U;

#if !defined(SYS_TICK_SECOND_DIV)
#define SYS_TICK_SECOND_DIV ((uint32_t)1000U)
#endif

// HSE_CLOCK_VALUE ->  Default value of the External oscillator in Hz
#if !defined(HSE_CLOCK_VALUE)
#define HSE_CLOCK_VALUE ((uint32_t)8000000)
#endif

// HSI_CLOCK_VALUE ->  Value of the Internal oscillator in Hz
#if !defined(HSI_CLOCK_VALUE)
#define HSI_CLOCK_VALUE ((uint32_t)16000000)
#endif

// Interrupt handler
void SysTick_Handler(void)
{
    s_system_time++;
}

void delay(const uint32_t sys_time_delta)
{
    s_timing_delay = s_system_time + sys_time_delta;
    while (s_timing_delay > s_system_time)
    {
    };
}

uint32_t getSysTime()
{
    return s_system_time;
}

uint32_t getSysTicksInSecond()
{
    return SYS_TICK_SECOND_DIV;
}

static void sysTickClockConfig()
{
    uint32_t system_core_clock = HSI_CLOCK_VALUE;
    const uint32_t sys_clk_source = (RCC->CFGR & RCC_CFGR_SWS);

    switch (sys_clk_source)
    {
    case RCC_CFGR_SWS_HSI:
    {
        // HSI used as system clock source
        system_core_clock = HSI_CLOCK_VALUE;
        break;
    }
    case RCC_CFGR_SWS_HSE:
    {
        // HSE used as system clock source
        system_core_clock = HSE_CLOCK_VALUE;
        break;
    }
    case RCC_CFGR_SWS_PLL:
    {

        // PLL used as system clock source
        const uint32_t pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
        const bool is_pll_source_hse = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) == RCC_PLLCFGR_PLLSRC_HSE);

        // PLL_VCO = (HSE_CLOCK_VALUE or HSI_CLOCK_VALUE / PLL_M) * PLL_N
        const uint32_t pllvco = ((is_pll_source_hse ? HSE_CLOCK_VALUE : HSI_CLOCK_VALUE) / pllm) *
                                ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos);

        // In catalog Pllp can be 2/4/6/8 based on register values 0/1/2/3, hence we need to do some multiplications
        const uint32_t pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> RCC_PLLCFGR_PLLP_Pos) + 1) * 2;

        // SYSCLK = PLL_VCO / PLL_P
        system_core_clock = pllvco / pllp;
        break;
    }
    default:
        break;
    }

    // Compute SysTick frequency
    const uint8_t AHB_PRESC_TABLE[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};

    // Frequency based on AHB prescaling table
    // In our case this should be 0 because we set the HPRE to 0
    // But let's keep a more generic implementation as it is in the default library
    system_core_clock >>= AHB_PRESC_TABLE[((RCC->CFGR & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos)];

    // Divide sec by 1000 to get SysTick at 1ms
    SysTick_Config(system_core_clock / SYS_TICK_SECOND_DIV);
}

static void flashMemoryLatencyConfig(void)
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

static void pllSystemClockConfig(void)
{
    // Enable HSE oscillator
    RCC->CR |= RCC_CR_HSEON;
    // Wait until HSE is ready
    while (!(RCC->CR & RCC_CR_HSERDY))
    {
    };

    // Set source of PLL to HSE
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLSRC;
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;

    // HSE = 8MHZ
    // PLLVCO = 8MHz/ 8PLLM * 336PLLN = 336MHz
    // SystemClock = 336 PLLVCO/2PLLP = 168MHz
    // USB Clock = 336 PLLVCO/7PLLQ = 48MHz

    // PLLM input DIVIDER 8 -> bring 8MHz HSE to 1MHz for stability
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM;
    RCC->PLLCFGR |= (8U << RCC_PLLCFGR_PLLM_Pos);

    // PLLN MULTIPLIER to 336
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN;
    RCC->PLLCFGR |= (336U << RCC_PLLCFGR_PLLN_Pos);

    // PLLP output DIVIDER = 2 (0 register value = DIV2)
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLP;

    // PLLQ DIVIDER = 7 -> 336MHz/7 = 48 MHZ -  USB OTG FS, SDIO and random number generator
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLQ;
    RCC->PLLCFGR |= (7U << RCC_PLLCFGR_PLLQ_Pos);

    // Enable PLL - 168MHz
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
    {
    };

    // Switch RCC source to PLL
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    // Wait for system clock source to be set to PLL
    while ((RCC->CFGR & RCC_CFGR_SWS) != (RCC_CFGR_SWS_PLL))
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
    // Pass clock to DMA
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    // Pass clock to CCMDataRam
    RCC->AHB1ENR |= RCC_AHB1ENR_CCMDATARAMEN;

    // ######## AHB2 ########
    // Pass clock to RNGEN
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;

    // ######## APB1 ########
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;

    // ######## APB2 ########
    // Pass clock to SPI1
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    // Pass clock to ADC1
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    // Pass clock to SDIO
    RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
}

void systemClockConfig(void)
{
    flashMemoryLatencyConfig();
    busClockConfig();
    pllSystemClockConfig();
    sysTickClockConfig();
    peripheralsClockEnable();
}