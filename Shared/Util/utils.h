#ifndef __UTILS_H
#define __UTILS_H

/*
 * Umbrella header for the shared Util helpers — include this to pull in all of them
 * at once (they are header-only, so unused ones cost nothing):
 *
 *   #include "Util/utils.h"
 *
 * Or include a single helper directly if you only need one. See Util/README.md.
 */

#include "Util/util_debounce.h"      /* Debouncer: settle a bouncing boolean */
#include "Util/util_edge_detector.h" /* EdgeDetector: fire on a rising/falling edge */

#endif /* __UTILS_H */
