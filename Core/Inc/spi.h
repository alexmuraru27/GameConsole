#ifndef __SPI_COMM_H
#define __SPI_COMM_H
#include <stdint.h>

void SPIInit(void);
void spiWrite(uint8_t data);
uint8_t spiRead();
#endif /* __SPI_COMM_H */