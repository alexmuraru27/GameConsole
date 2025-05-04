#ifndef __GPIO_H
#define __GPIO_H
#include <stdint.h>

void gpioInit(void);
void gpioSpi1RstLow(void);
void gpioSpi1RstHigh(void);
void gpioSpi1DcLow(void);
void gpioSpi1DcHigh(void);
#endif /* __GPIO_H */