#include <stm32f407xx.h>
#include <sys/stat.h>
#include "swo.h"      /* swoInit prototype (shared with the console) */
#include "sysclock.h" /* getSysTime prototype (logger.c depends on it) */

/*
 * Minimal SWO/ITM trace glue for the bootloader, so it can reuse the console's
 * logger.c verbatim (same "[tick][L][CHAN] msg" format over SWO). The bootloader
 * runs at the reset-default HSI clock (16 MHz, no PLL), so the TPIU prescaler is
 * computed against that — targeting the same 2 MHz SWO the host decodes
 * (tools/scripts/swo.sh). The app re-inits SWO for 168 MHz when it boots.
 */
#define BOOT_HCLK_HZ 16000000U /* HSI after reset; the bootloader never raises it */

void swoInit(uint32_t swoFreqHz)
{
    const uint32_t prescaler = (BOOT_HCLK_HZ / swoFreqHz) - 1U;

    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN;  /* enable TRACESWO pin */
    DBGMCU->CR &= ~DBGMCU_CR_TRACE_MODE; /* 00 = asynchronous SWO */

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    TPI->ACPR = prescaler; /* HCLK / (ACPR+1) = SWO baud */
    TPI->SPPR = 2;         /* SWO NRZ (UART-like) */
    TPI->FFCR = 0x00;

    ITM->LAR = 0xC5ACCE55;
    ITM->TCR = (1UL << ITM_TCR_ITMENA_Pos) |
               (1UL << ITM_TCR_SYNCENA_Pos) |
               (0x01UL << 16); /* TraceBusID = 1 */
    ITM->TER = 1;             /* enable stimulus port 0 */

    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0U; /* zero the cycle counter so getSysTime() starts at boot */
}

/* Milliseconds since swoInit(), from the DWT cycle counter (the bootloader has no
 * SysTick). 32-bit CYCCNT wraps after ~268 s at 16 MHz — far longer than any
 * bootloader run. */
uint32_t getSysTime(void)
{
    return DWT->CYCCNT / (BOOT_HCLK_HZ / 1000U);
}

/* printf() sink: route each byte to SWO via ITM, exactly like the console's
 * syscalls.c. Lets logger.c / printf work with no other plumbing. */
int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++)
    {
        ITM_SendChar((uint32_t)(uint8_t)ptr[i]);
    }
    return len;
}

/* Make stdout a character device so newlib line-buffers it (flush on '\n')
 * instead of fully buffering — otherwise log lines would sit in a ~1 KB buffer
 * that never fills in the bootloader's short run. Mirrors the console's syscalls.c. */
int _isatty(int file)
{
    (void)file;
    return 1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}
