#include "sysclock.h"
#include <stdbool.h>
#include "stm32f407xx.h"

static volatile uint32_t s_timing_delay = 0U;
static volatile uint32_t s_system_time = 0U;

/* Optional per-tick hook (see sysclockSetTickHook). NULL until the kernel arms it. */
static void (*s_tick_hook)(uint32_t) = 0;

void sysclockSetTickHook(void (*hook)(uint32_t))
{
    s_tick_hook = hook;
}

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

// DWT cycle counter ticks per microsecond at the 168 MHz SYSCLK
// (pllSystemClockConfig) — the basis for delayUs().
#define CPU_CYCLES_PER_US 168U

// Interrupt handler
void SysTick_Handler(void)
{
    s_system_time++;
    /* Enforce the running game callback's time budget (the kernel registers a hook;
     * no-op before any game runs or outside a game). */
    if (s_tick_hook != 0)
    {
        s_tick_hook(s_system_time);
    }
}

void delay(const uint32_t sys_time_delta)
{
    s_timing_delay = s_system_time + sys_time_delta;
    /* Wrap-safe: compare the signed difference so the wait still ends correctly when
     * s_system_time rolls over uint32 (~49.7 days of uptime) mid-delay. */
    while ((int32_t)(s_timing_delay - s_system_time) > 0)
    {
    };
}

void delayUs(uint32_t us)
{
    /* Short busy-wait off the free-running 168 MHz DWT cycle counter, for the
     * sub-millisecond delays the SysTick-based delay() can't express (peripheral
     * stabilization, I2C bus-recovery clocking). swoInit() enables CYCCNT early in
     * boot, but enable it here too so the function is self-sufficient: without a
     * running counter the wrap-safe compare below would never advance and spin
     * forever. Intended for short waits (us * 168 must fit in 32 bits). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = us * CPU_CYCLES_PER_US;
    while ((DWT->CYCCNT - start) < cycles)
    {
    }
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
    /* The clock tree is fixed (pllSystemClockConfig runs first and sets SYSCLK to
     * SYSCLK_HZ with AHB /1), so the SysTick reload is simply SYSCLK_HZ per second
     * divided down to the 1 ms tick — no need to reverse-engineer it from RCC. */
    SysTick_Config(SYSCLK_HZ / SYS_TICK_SECOND_DIV);
}

static void flashMemoryLatencyConfig(void)
{
    /* 5 wait states for 168 MHz at 2.7-3.6V (RM0090 Table), and turn on the full
     * ART accelerator: prefetch + the instruction cache + the data cache. All
     * flash-resident code (the renderer, FatFs, the menus) and its rodata reads
     * are otherwise paying the 5-WS fetch penalty every miss. The caches are
     * empty out of reset, so they can be enabled directly alongside the latency.
     * Coherency after a flash *write* (the OS self-flash readback-verify) is
     * handled by resetting the data cache in flashLlLock(). */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_5WS;
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
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    // i2c1
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // ######## AHB3 ########
    // Pass clock to FSMC
    RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;

    // ######## APB2 ########
    // Pass clock to ADC1
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    // Pass clock to SDIO
    RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
    // usart1 (ESP-01 link)
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    // tim9 (PA3 backlight PWM)
    RCC->APB2ENR |= RCC_APB2ENR_TIM9EN;
}

void enableFPU(void)
{
    /* Enable FPU coprocessors CP10/CP11 — required because we compile with
     * -mfloat-abi=hard and GCC may emit VFP instructions (e.g. vstr for bulk
     * zeroing).  Without this, a NOCP UsageFault escalates to HardFault. */
    SCB->CPACR |= ((3UL << (10U * 2U)) | (3UL << (11U * 2U)));
    __DSB();
    __ISB();
}
void systemClockConfig(void)
{
    enableFPU();
    flashMemoryLatencyConfig();
    busClockConfig();
    pllSystemClockConfig();
    sysTickClockConfig();
    peripheralsClockEnable();
}