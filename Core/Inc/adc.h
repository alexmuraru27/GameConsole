#ifndef __ADC_COMM_H
#define __ADC_COMM_H
#include <stdint.h>

void adcInit(void);
volatile uint16_t *getAdc1BufferAddress();
uint8_t getAdc1BufferSize();
#endif /* __ADC_COMM_H */