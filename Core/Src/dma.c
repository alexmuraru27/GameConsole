#include "dma.h"
#include "usart.h"
#include <stm32f407xx.h>

// Usart2 DMA1
//  -    USART2_RX: Ch4 S5
//  -    USART2_TX: Ch4 S6

// SPI1 DMA2
//  -    TX:  Ch3 S5
//  -    RX:  Ch3 S2

// SDIO DMA2: Ch4 S3

// ADC1 DMA2: Ch0, S0
extern volatile uint8_t g_usart2_dma_busy;
extern volatile uint8_t g_usart2_active_buffer_pos;
void DMA1_Stream6_IRQHandler(void)
{
    if (DMA1->HISR & DMA_HISR_TCIF6)
    {
        DMA1->HIFCR |= DMA_HIFCR_CTCIF6; // Clear interrupt
        g_usart2_dma_busy = 0;
        if (g_usart2_active_buffer_pos > 0)
        {
            usart2BufferFlush(); // Immediately send accumulated data
        }
    }
}

static void usart2DmaInit(void)
{
    // disable DMA1 S6
    DMA1_Stream6->CR &= ~DMA_SxCR_EN;
    // set peripheral data register to USART2-<DR
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
    // reenable DMA1 S6
    DMA1_Stream6->CR |= DMA_SxCR_EN;
    // enable interrupt in NVIC
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

void dmaInit(void)
{
    usart2DmaInit();
}