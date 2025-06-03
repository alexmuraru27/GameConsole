#include "dac.h"
#include <stm32f407xx.h>

void dacInit(void)
{
    // disable dac
    DAC->CR &= ~DAC_CR_EN1;
    // timer6
    DAC->CR &= ~DAC_CR_TSEL1;
    // enable trigger
    DAC->CR |= DAC_CR_TEN1;
    // enable buffer -> high currents
    DAC->CR &= ~DAC_CR_BOFF1;
    // enable dac
    DAC->CR |= DAC_CR_EN1;
}

void dacWrite(uint8_t value)
{
    DAC->DHR8R1 = value;
}
