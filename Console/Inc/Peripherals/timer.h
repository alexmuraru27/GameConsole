#ifndef __TIMER_COMM_H
#define __TIMER_COMM_H
#include <stdint.h>

void timerInit(void);
void timer3Disable(void);
void timer3Trigger(uint32_t frequency_hz, uint8_t duty);
#endif /* __TIMER_COMM_H */