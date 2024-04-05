#include "stm32f407xx.h"
extern uint32_t _stext;

volatile uint32_t timing_delay = 0U;
void delayMSSysTick(uint32_t time)
{
    timing_delay = time;
    while (timing_delay != 0)
    {
    };
}

void SysTick_Handler(void)
{
    if (timing_delay != 0)
    {
        timing_delay--;
    }
}

void SystemClockConfig(void)
{
    // No bypass, using internal clock
    RCC->CR &= ~RCC_CR_HSEBYP;

    // Enable HSE
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
    {
    }; // Wait until HSE is ready

    // ############## FLASH
    // Add prefetch to instructions with latency of 5 cycles according to catalog
    FLASH->ACR |= FLASH_ACR_PRFTEN;
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_5WS;

    // ############## BUS PRESCALERS
    // AHB Prescaler = 1
    RCC->CFGR &= ~RCC_CFGR_HPRE;

    // APB1 - LOWSPEED = AHB SPEED/4
    RCC->CFGR &= ~RCC_CFGR_PPRE1;
    RCC->CFGR |= RCC_CFGR_PPRE1_DIV4;

    // APB2 - HIGHSPEED = AHB SPEED/2
    RCC->CFGR &= ~RCC_CFGR_PPRE2;
    RCC->CFGR |= RCC_CFGR_PPRE2_DIV2;

    // ############## PLL
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

    // ################## peripherals clock enable ##################
    // ######## AHB1 ########
    // Pass clock to GPIOA
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
}

void GPIOConfig(void)
{
    GPIOA->MODER |= (1 << GPIO_MODER_MODER6_Pos);
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6;
    GPIOA->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED6_Pos);
}

void BoardInit(void)
{
    SystemClockConfig();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);
    GPIOConfig();
}

int main(void)
{
    // Sets the The Vector Table Offset Register to the App text memory section
    uint32_t *pSrc = (uint32_t *)&_stext;
    SCB->VTOR = ((uint32_t)pSrc & SCB_VTOR_TBLOFF_Msk);
    __enable_irq();

    BoardInit();

    GPIOA->MODER |= (1 << GPIO_MODER_MODER6_Pos);
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6_Msk;
    GPIOA->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED6_Pos);

    while (1)
    {
        GPIOA->BSRR |= (1 << 6);
        delayMSSysTick(250U);
        GPIOA->BSRR |= (1 << 6) << 16;
        delayMSSysTick(100U);
    }
}