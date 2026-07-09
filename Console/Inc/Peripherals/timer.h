#ifndef __TIMER_H
#define __TIMER_H
#include <stdint.h>

void timerInit(void);
void timer3Disable(void);
void timer3Trigger(uint32_t frequency_hz);
#endif /* __TIMER_H */