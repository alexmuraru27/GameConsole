#ifndef __SYS_CLOCK_CONFIG_H
#define __SYS_CLOCK_CONFIG_H
#include <stdint.h>
#include <stm32f407xx.h>

/* The clock tree programmed by systemClockConfig(): HSE 8 MHz → PLL → 168 MHz
 * SYSCLK, AHB /1, APB1 /4, APB2 /2. Every driver's timing math is a consequence
 * of these, so they live here (the single source) instead of as bare literals
 * scattered across the peripheral drivers. On an APBx timer clock: when the APB
 * prescaler is not /1 the timer clock is 2×PCLKx (STM32F4 RM0090). */
#define SYSCLK_HZ          168000000U
#define PCLK1_HZ           42000000U /* APB1 peripheral clock  (SYSCLK / 4) — I2C1 */
#define PCLK2_HZ           84000000U /* APB2 peripheral clock  (SYSCLK / 2) — USART1 */
#define APB1_TIMER_CLK_HZ  84000000U /* TIM3/6/7 timer clock   (2 × PCLK1) */
#define APB2_TIMER_CLK_HZ  168000000U /* TIM9 timer clock      (2 × PCLK2) */
#define SDIO_CK_INPUT_HZ   48000000U /* SDIO adapter input from the 48 MHz PLLQ */

void systemClockConfig(void);

/* Register a hook invoked from SysTick_Handler every 1 ms with the current
 * millisecond count (or NULL to clear). Lets a higher layer (the kernel's game
 * liveness deadline) run on the tick without the timebase depending on it. */
void sysclockSetTickHook(void (*hook)(uint32_t sys_time));

void delay(uint32_t sys_time_delta);
/* Short (sub-millisecond) busy-wait off the DWT cycle counter — for peripheral
 * stabilization and bit-bang timing the 1 ms SysTick delay() can't express. */
void delayUs(uint32_t us);
uint32_t getSysTime(void);
uint32_t getSysTicksInSecond();

#endif /* __SYS_CLOCK_CONFIG_H */