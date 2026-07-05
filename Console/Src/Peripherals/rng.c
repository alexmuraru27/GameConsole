#include "rng.h"
#include <stm32f407xx.h>
#include "logger.h"

/* Bound on the DRDY spin. The RNG emits a new 32-bit word every ~40 RNG-clock
 * periods, so DRDY is set within a few dozen loop iterations; this cap only
 * guards against a lost RNG clock (which cannot happen here — PLLQ/48 MHz is up
 * for SDIO) so a game syscall can never wedge the console. */
#define RNG_READY_TIMEOUT 100000U

void rngInit(void)
{
    /* The AHB2 bus clock to the RNG is enabled in systemClockConfig(); turn on the
     * generator. The first samples are produced a few dozen RNG cycles later. */
    RNG->CR |= RNG_CR_RNGEN;
    LOGGER_LOG_INFO(LOGGER_CORE, "RNG up: hardware TRNG enabled");
}

uint32_t rngGetRandom(void)
{
    /* A seed error (entropy source produced a suspicious run) or clock error
     * latches in SR; clear it and restart the generator, then read fresh. */
    if (RNG->SR & (RNG_SR_SEIS | RNG_SR_CEIS))
    {
        RNG->SR &= ~(RNG_SR_SEIS | RNG_SR_CEIS);
        RNG->CR &= ~RNG_CR_RNGEN;
        RNG->CR |= RNG_CR_RNGEN;
    }

    uint32_t spins = RNG_READY_TIMEOUT;
    while (!(RNG->SR & RNG_SR_DRDY))
    {
        if (--spins == 0U)
        {
            /* Should be unreachable: the RNG clock is always present. Fall back to
             * the free-running cycle counter (hashed) so the call still returns a
             * non-blocking, decorrelated value instead of hanging the syscall. */
            LOGGER_LOG_WARN(LOGGER_CORE, "RNG DRDY timeout; cycle-counter fallback");
            return DWT->CYCCNT * 2654435761U; /* Knuth multiplicative hash */
        }
    }
    return RNG->DR;
}
