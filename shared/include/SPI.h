#ifndef __SPI_H
#define __SPI_H

#include "stm32f407xx.h"
#include "string.h"

void SPI2_Init(void);
void SPI2_WriteUint8Buffer(uint8_t *buff, size_t buff_size);
void SPI2_WriteUint16ReversedBuffer(uint16_t *buff, size_t buff_size);

#endif /* __SPI_H */