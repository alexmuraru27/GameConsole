#ifndef __SYS_CLOCK_CONFIG_H
#define __SYS_CLOCK_CONFIG_H

#include "stm32f407xx.h"

void system_clock_config(void);
void delay_ms_systick(uint32_t time);

#endif /* __SYS_CLOCK_CONFIG_H */