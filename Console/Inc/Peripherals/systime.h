#ifndef __PERIPHERALS_SYSTIME_H
#define __PERIPHERALS_SYSTIME_H

#include <stdint.h>

/*
 * Everyday millisecond timebase — the SysTick-driven clock and the short busy-wait.
 * Split out of sysclock.h (which pulls in the full CMSIS device header for the
 * one-time clock-tree bring-up) so the many callers that only want a timestamp or a
 * delay don't inherit stm32f407xx.h. The counter is started by systemClockConfig().
 */

/* Block for `sys_time_delta` milliseconds (SysTick-based, wrap-safe). */
void delay(uint32_t sys_time_delta);

/* Short (sub-millisecond) busy-wait off the DWT cycle counter — for peripheral
 * stabilization and bit-bang timing the 1 ms SysTick delay() can't express. */
void delayUs(uint32_t us);

/* Milliseconds since boot (monotonic; wraps at ~49.7 days). */
uint32_t getSysTime(void);

/* SysTick ticks per second (1000). */
uint32_t getSysTicksInSecond(void);

#endif /* __PERIPHERALS_SYSTIME_H */
