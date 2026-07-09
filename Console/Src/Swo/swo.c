#include "Swo/swo.h"
#include <stm32f407xx.h>
#include "Logger/logger.h"

void swoInit(uint32_t swoFreqHz)
{
    uint32_t SWOPrescaler = (RCC_MAX_FREQUENCY / swoFreqHz) - 1;

    /* Enable SWO output pin (TRACESWO). Without this the TPI cannot
     * drive the ITM FIFO never drains, and every printf spin-waits. */
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN;
    DBGMCU->CR &= ~DBGMCU_CR_TRACE_MODE; /* 00 = asynchronous SWO */

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    TPI->ACPR = SWOPrescaler;
    TPI->SPPR = 2;
    TPI->FFCR = 0x00;

    ITM->LAR = 0xC5ACCE55;
    ITM->TCR = (1UL << ITM_TCR_ITMENA_Pos) |
               (1UL << ITM_TCR_SYNCENA_Pos) |
               (0x01UL << 16); // TraceBusID = 1
    ITM->TER = 1;

    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* SWO/ITM is live from here — this is the first line that can be traced. */
    LOGGER_LOG_DEBUG(LOGGER_CORE, "SWO/ITM up @ %lu Hz", (unsigned long)swoFreqHz);
}