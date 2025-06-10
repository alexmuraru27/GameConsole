#include "dma.h"
#include "usart.h"
#include "adc.h"
#include <stm32f407xx.h>

// Usart2 DMA1
//  -    USART2_RX: Ch4 S5
//  -    USART2_TX: Ch4 S6

// SPI1 DMA2
//  -    TX:  Ch3 S5
//  -    RX:  Ch3 S2

// SDIO DMA2: Ch4 S3

// ADC1 DMA2: Ch0, S0
void DMA1_Stream6_IRQHandler(void)
{
    if (DMA1->HISR & DMA_HISR_TCIF6)
    {
        DMA1->HIFCR |= DMA_HIFCR_CTCIF6; // Clear interrupt
        usart2SetDmaFree();
    }
}

static void usart2DmaInit(void)
{
    // disable DMA1 S6
    DMA1_Stream6->CR &= ~DMA_SxCR_EN;
    // Wait for DMA to be disabled
    while (DMA1_Stream6->CR & DMA_SxCR_EN)
        ;

    // set peripheral data register to USART2->DR
    DMA1_Stream6->PAR = (uint32_t)&USART2->DR;
    // select channel 4(Usart2_TX)
    DMA1_Stream6->CR |= 4U << DMA_SxCR_CHSEL_Pos;
    // memory 1 transfer/Peripheral 1 transfer/memory uint8/peripheral uint8
    DMA1_Stream6->CR &= ~(DMA_SxCR_MBURST | DMA_SxCR_PBURST | DMA_SxCR_MSIZE | DMA_SxCR_PSIZE);
    // memory increment after each transfer by 1(msize)
    DMA1_Stream6->CR |= DMA_SxCR_MINC;
    // direction from memory to peripheral
    DMA1_Stream6->CR |= 1U << DMA_SxCR_DIR_Pos;
    // enable interrupt: transfer complete
    DMA1_Stream6->CR |= DMA_SxCR_TCIE;

    // DMA should not be enabled without data!
    // enable interrupt in NVIC
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

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
    usart2DmaInit();
    adc1DmaInit();
}