#ifndef __DAC_COMM_H
#define __DAC_COMM_H
#include <stdint.h>

void dacInit(void);
void dacWrite(uint8_t value);
#endif /* __DAC_COMM_H */