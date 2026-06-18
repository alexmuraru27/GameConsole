#include "dma.h"
#include "adc.h"
#include <stm32f407xx.h>
#include <stdbool.h>
#include "logger.h"

static bool s_fsmc_dma_active = false;

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

static void fsmcDmaInit(uint32_t fsmcDataAddress)
{
    // FSMC DMA: DMA2 S6Ch0 M2M, PAR=source (inc), M0AR=LCD data (fixed)
    DMA2_Stream6->CR = 0;
    while (DMA2_Stream6->CR & DMA_SxCR_EN)
        ;

    DMA2_Stream6->M0AR = fsmcDataAddress;
    /* Memory-to-memory transfers MUST use the FIFO: on the STM32F4 direct mode
     * is not allowed for M2M (RM0090). Running M2M in direct mode is out of spec
     * and corrupts transfers intermittently (stray scanlines). Disable direct
     * mode (enable the FIFO) at the full threshold. */
    DMA2_Stream6->FCR = DMA_SxFCR_DMDIS | DMA_SxFCR_FTH;
    DMA2_Stream6->CR = (0U << DMA_SxCR_CHSEL_Pos) | // channel 0
                       DMA_SxCR_PL_1 |              // priority high
                       DMA_SxCR_MSIZE_0 |           // dest 16-bit
                       DMA_SxCR_PSIZE_0 |           // source 16-bit
                       DMA_SxCR_PINC |              // increment source
                       (2U << DMA_SxCR_DIR_Pos);    // memory-to-memory
}

void fsmcDmaSend(const uint16_t *data, uint32_t count)
{
    DMA2_Stream6->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream6->CR & DMA_SxCR_EN)
        ;
    DMA2->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTEIF6 |
                  DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CFEIF6;
    DMA2_Stream6->PAR = (uint32_t)data;
    DMA2_Stream6->NDTR = count;
    DMA2_Stream6->CR |= DMA_SxCR_EN;
    s_fsmc_dma_active = true;
}

void fsmcDmaWait(void)
{
    if (!s_fsmc_dma_active)
    {
        return;
    }
    while (!(DMA2->HISR & DMA_HISR_TCIF6))
    {
    }
    s_fsmc_dma_active = false;
}

void dmaInit(uint32_t fsmcDataAddress)
{
    adc1DmaInit();
    fsmcDmaInit(fsmcDataAddress);
    LOGGER_LOG_DEBUG(LOGGER_CORE, "DMA init: ADC1 (DMA2 S0) + FSMC (DMA2 S6)");
}