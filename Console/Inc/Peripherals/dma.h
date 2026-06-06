#ifndef __DMA_COMM_H
#define __DMA_COMM_H
#include <stdint.h>

void dmaInit(uint32_t fsmcDataAddress);
void fsmcDmaSend(const uint16_t *data, uint32_t count);
void fsmcDmaWait(void);
#endif /* __DMA_COMM_H */