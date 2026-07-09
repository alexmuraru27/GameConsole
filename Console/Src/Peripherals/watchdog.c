#include "Peripherals/watchdog.h"
#include <stm32f407xx.h>
#include "Logger/logger.h"

/* IWDG key-register magic values (RM0090 sec. 21.4.1). */
#define IWDG_KEY_RELOAD 0x0000AAAAU /* refresh the down-counter (the "kick")        */
#define IWDG_KEY_ENABLE 0x0000CCCCU /* start the IWDG (also turns on the LSI)        */
#define IWDG_KEY_ACCESS 0x00005555U /* unlock PR/RLR for writing                    */

/*
 * Timeout ~10 s nominal. The IWDG is clocked by the LSI, which is 32 kHz typical
 * but specced 17-47 kHz over process/voltage/temperature, so the real timeout
 * spans ~6.8 s (fast LSI) to ~19 s (slow LSI). The lower bound is what matters:
 * it must clear the longest stretch the main flow can go without a kick. The
 * worst such stretch is a single 128 KB flash sector erase, which runs with
 * interrupts masked for up to ~4 s (datasheet max) — so 10 s nominal keeps a
 * safe margin even at the fast-LSI end. A genuine hang therefore resets the
 * console within ~7-19 s; the kernel's 2 s per-callback deadline handles the
 * common (runaway-game) case long before that, and gracefully.
 *
 *   t = (prescaler / f_LSI) * (RLR + 1)
 *   prescaler /256 -> 32000/256 = 125 Hz -> 8 ms/tick; RLR = 1249 -> ~10 s.
 */
#define IWDG_PRESCALER_DIV256 0x6U /* PR[2:0] = 6 selects /256 */
#define IWDG_RELOAD_TICKS 1249U

void watchdogInit(void)
{
    /* Surface (and clear) an IWDG-induced reset so a watchdog recovery shows up in
     * the boot log instead of looking like an ordinary power-on. */
    if (RCC->CSR & RCC_CSR_IWDGRSTF)
    {
        LOGGER_LOG_WARN(LOGGER_CORE, "recovered from watchdog reset (system was wedged)");
    }
    RCC->CSR |= RCC_CSR_RMVF; /* clear the reset-cause flags */

    /* Freeze the IWDG while the core is halted at a breakpoint, so debugging
     * doesn't trip a spurious reset (no effect when no debugger is attached). */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    IWDG->KR = IWDG_KEY_ENABLE; /* start IWDG + LSI */
    IWDG->KR = IWDG_KEY_ACCESS; /* enable write access to PR/RLR */
    IWDG->PR = IWDG_PRESCALER_DIV256;
    IWDG->RLR = IWDG_RELOAD_TICKS;
    while (IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) /* wait for PR/RLR to take */
    {
    }
    IWDG->KR = IWDG_KEY_RELOAD; /* load the counter from RLR and begin */

    LOGGER_LOG_INFO(LOGGER_CORE, "IWDG armed (~10s timeout, /256, reload %u)", (unsigned)IWDG_RELOAD_TICKS);
}

void watchdogKick(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}
