#include "clock/clock_config.h"

static volatile uint32_t timing_delay = 0U;

// Interrupt handler
void SysTick_Handler(void)
{
    if (timing_delay != 0)
    {
        timing_delay--;
    }
}

static void CMSIS_clock_config(void)
{
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);
}

static void select_clock_source_config(void)
{
    // No bypass, using internal clock
    RCC->CR &= ~RCC_CR_HSEBYP;

    // Enable HSE
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
    {
    }; // Wait until HSE is ready
}

static void flash_memory_clock_config(void)
{
    FLASH->ACR |= FLASH_ACR_PRFTEN;
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_5WS;
}

static void bus_clock_config(void)
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

static void pll_clock_config(void)
{
    // AHB PLL source mux from HSE 0 not divided, 1 divided
    RCC->CFGR &= ~RCC_CFGR_HPRE;

    // SET source of PLL to HSE
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLSRC;
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;

    // PLLM input DIVIDER to 8U -> bring it down to 1MHz for stability
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLM;
    RCC->PLLCFGR |= (4U << RCC_PLLCFGR_PLLM_Pos);

    // PLLN MULTIPLIER to 168
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLN;
    RCC->PLLCFGR |= (168 << RCC_PLLCFGR_PLLN_Pos);

    // PLLP output DIVIDER = 2
    RCC->PLLCFGR &= ~RCC_PLLCFGR_PLLP;

    // PLLQ DIVIDER = 4 -  USB OTG FS, SDIO and random number generator
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

static void peripherals_clock_enable(void)
{
    // ######## AHB1 ########
    // Pass clock to GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Pass clock to GPIOC
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    // Pass clock to DMA controllers
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    // ######## APB1 ########
    // Enable clock for UART4
    RCC->APB1ENR |= RCC_APB1ENR_UART4EN;

    // ######## APB2 ########
    // Enable clock for ADC1
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
}

void delay_ms_systick(uint32_t time)
{
    timing_delay = time;
    while (timing_delay != 0)
    {
    };
}

void system_clock_config(void)
{
    select_clock_source_config();
    flash_memory_clock_config();
    bus_clock_config();
    pll_clock_config();
    peripherals_clock_enable();
    CMSIS_clock_config();
}