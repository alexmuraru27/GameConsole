#ifndef __UTIL_EDGE_DETECTOR_H
#define __UTIL_EDGE_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

/*
 * EdgeDetector — fires on the configured transition of an (already clean) boolean.
 * Header-only (static inline), shared by the console and loaded games. Declare one
 * statically (it keeps the previous level) and feed it one sample per poll:
 *
 *   EDGE_DETECTOR_DECLARE(s_jump, EDGE_RISING);
 *   bool pressed = edgeUpdate(&s_jump, held);   // true the poll `held` goes 0 -> 1
 *
 * For a bouncy source, feed a Debouncer's output in (see Util/util_debounce.h).
 * Declare one with EDGE_DETECTOR_DECLARE. See Util/README.md for more.
 *
 * NOTE: edgeUpdate() advances its state, so call it exactly once per poll and NOT
 * behind a short-circuiting `||`/`&&` that might skip it — compute it into a local
 * first, then use the local.
 */
typedef enum
{
    EDGE_RISING = 1,  /* false -> true */
    EDGE_FALLING = 2, /* true -> false */
    EDGE_BOTH = 3,    /* either transition */
} EdgeType;

typedef struct
{
    uint8_t type; /* EdgeType */
    bool prev;    /* level at the previous update */
} EdgeDetector;

/* File/function-scope edge detector for the given EdgeType. Assumes the signal
 * starts low (prev = false), so a level already high at the first update reads as a
 * rising edge — seed with a prior update if that is not wanted. The initializer is
 * positional — field order: type, prev. */
#define EDGE_DETECTOR_DECLARE(name, type_) static EdgeDetector name = {(uint8_t)(type_), false}

/* Feed one sample; returns true on the configured edge. */
static inline bool edgeUpdate(EdgeDetector *e, bool level)
{
    bool fired = false;
    if (level && !e->prev && (e->type & (uint8_t)EDGE_RISING))
    {
        fired = true;
    }
    else if (!level && e->prev && (e->type & (uint8_t)EDGE_FALLING))
    {
        fired = true;
    }
    e->prev = level;
    return fired;
}

#endif /* __UTIL_EDGE_DETECTOR_H */
