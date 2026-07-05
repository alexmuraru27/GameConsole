#ifndef __RNG_H
#define __RNG_H

#include <stdint.h>

/* Hardware true-random-number generator (RNG peripheral). The AHB2 clock to the
 * RNG and the 48 MHz PLLQ that feeds its entropy source are configured in
 * systemClockConfig(); rngInit() enables the generator itself. */
void rngInit(void);

/* Return a fresh 32-bit true-random value. Blocks briefly (~tens of cycles) for
 * the next sample; self-recovers from a seed/clock error. Exposed to games as the
 * getRandom() syscall. */
uint32_t rngGetRandom(void);

#endif /* __RNG_H */
