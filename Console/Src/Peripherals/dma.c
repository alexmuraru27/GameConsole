#include "dma.h"
#include "adc.h"
#include <stm32f407xx.h>

// SDIO DMA2: Ch4 S3

// ADC1 DMA2: Ch0, S0
static void adc1DmaInit(void)
{
    // configure DMA2 S0Ch1 for ADC1
    DMA2_Stream0->CR = 0;
    while (DMA2_Stream0->CR & DMA_SxCR_EN)
        ;

    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;               // peripheral address
    DMA2_Stream0->M0AR = (uint32_t)getAdc1BufferAddress(); // memory 0 address
    DMA2_Stream0->NDTR = (uint32_t)getAdc1BufferSize();    // 4 data items
    DMA2_Stream0->CR = (0U << DMA_SxCR_CHSEL_Pos) |        // channel 0
                       DMA_SxCR_PL_1 |                     // priority high
                       DMA_SxCR_MSIZE_0 |                  // memory size = 16-bit
                       DMA_SxCR_PSIZE_0 |                  // peripheral size = 16-bit
                       DMA_SxCR_MINC |                     // memory increment
                       DMA_SxCR_CIRC;                      // circular mode

    // enable DMA
    DMA2_Stream0->CR |= DMA_SxCR_EN;
}
void dmaInit(void)
{
    adc1DmaInit();
}