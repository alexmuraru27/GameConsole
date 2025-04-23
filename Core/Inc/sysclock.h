#ifndef __SYS_CLOCK_CONFIG_H
#define __SYS_CLOCK_CONFIG_H
#include <stdint.h>
#include <stm32f407xx.h>

void systemClockConfig(void);
void delayMs(uint32_t time);
uint32_t getSysTime(void);

#endif /* __SYS_CLOCK_CONFIG_H */