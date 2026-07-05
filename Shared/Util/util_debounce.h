#ifndef __UTIL_DEBOUNCE_H
#define __UTIL_DEBOUNCE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Debouncer — a raw (bouncing) value is only accepted once it has held the same
 * value for `stable_ms`. Header-only (static inline), shared by the console and
 * loaded games.
 *
 * Generic over width: the value is carried as uint32_t, so it debounces a bool,
 * uint8_t, uint16_t or uint32_t interchangeably — a narrower input promotes in, and
 * the returned value casts back out. Declare one statically (it keeps its own state)
 * and feed it one sample per poll with the current time + raw value:
 *
 *   DEBOUNCER_DECLARE(s_start, 5);                       // accept a value after 5 ms
 *   bool     held = debounceUpdate(&s_start, now_ms, rawStartPin());   // bool source
 *   uint8_t  mode = debounceUpdate(&s_mode,  now_ms, rawModeSwitch()); // uint8 source
 *
 * Declare one with DEBOUNCER_DECLARE. See Util/README.md for more.
 */
typedef struct
{
    uint32_t stable_ms; /* the raw value must hold this long to be accepted */
    uint32_t since_ms;  /* when the current candidate value was first seen */
    uint32_t candidate; /* the raw value currently being timed */
    uint32_t state;     /* the accepted (debounced) value */
} Debouncer;

/* File/function-scope debouncer that accepts a value after it holds `stable_ms_`.
 * Starts at 0; the first debounceUpdate() seeds the settle timer. The initializer is
 * positional — field order: stable_ms, since_ms, candidate, state. */
#define DEBOUNCER_DECLARE(name, stable_ms_) static Debouncer name = {(stable_ms_), 0U, 0U, 0U}

/* Feed one sample; returns the debounced value. `now_ms` is any millisecond clock
 * (e.g. getSysTime()); the elapsed compare is wrap-safe. */
static inline uint32_t debounceUpdate(Debouncer *d, uint32_t now_ms, uint32_t raw)
{
    if (raw != d->candidate)
    {
        d->candidate = raw; /* the value changed — restart the settle timer */
        d->since_ms = now_ms;
    }
    else if (raw != d->state && (uint32_t)(now_ms - d->since_ms) >= d->stable_ms)
    {
        d->state = raw; /* it held long enough — accept it */
    }
    return d->state;
}

/* The current debounced value without feeding a new sample. */
static inline uint32_t debounceGet(const Debouncer *d)
{
    return d->state;
}

#endif /* __UTIL_DEBOUNCE_H */
