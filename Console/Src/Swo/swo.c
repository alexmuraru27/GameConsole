#include "swo.h"

void swoInit(uint32_t swoFreqHz)
{
    uint32_t SWOPrescaler = (RCC_MAX_FREQUENCY / swoFreqHz) - 1;

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
}