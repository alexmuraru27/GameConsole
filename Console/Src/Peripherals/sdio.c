#include "sdio.h"
#include "stm32f407xx.h"

void sdioInit(void)
{
    // power
    SDIO->POWER = 0x03U;
    // from catalog SDIO_CK frequency = SDIOCLK / [CLKDIV + 2].
    // 48MHz/ (118+2) = 400KHz
    SDIO->CLKCR = 118U;
    // Enable clock
    SDIO->CLKCR |= SDIO_CLKCR_CLKEN;
}

// ### SD-CARD (Builtin)
// PD2 (SDIO_CMD)(CMD) - AF12
// PC8 (SDIO_D0)(DAT0) - AF12
// PC9 (SDIO_D1)(DAT1) - AF12
// PC10 (SDIO_D2)(DAT2) - AF12
// PC11 (SDIO_D3)(CD/DAT3) - AF12
// PC12 (SDIO_CK)(CLK) - AF12