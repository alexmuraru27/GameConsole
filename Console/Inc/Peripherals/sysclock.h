#ifndef __SYS_CLOCK_CONFIG_H
#define __SYS_CLOCK_CONFIG_H
#include <stdint.h>
#include <stm32f407xx.h>

void systemClockConfig(void);
void delay(uint32_t sys_time_delta);
/* Short (sub-millisecond) busy-wait off the DWT cycle counter — for peripheral
 * stabilization and bit-bang timing the 1 ms SysTick delay() can't express. */
void delayUs(uint32_t us);
uint32_t getSysTime(void);
uint32_t getSysTicksInSecond();

#endif /* __SYS_CLOCK_CONFIG_H */