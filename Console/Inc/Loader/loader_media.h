#ifndef __LOADER_MEDIA_H
#define __LOADER_MEDIA_H

#include <stdbool.h>

/* ------------------------------------------------------------------ *
 *  Removable-media (SD card) state. loaderMediaInit() mounts the card
 *  at boot. loaderMediaSync() is polled by the menu each frame: it
 *  debounces the card-detect line and, on a committed insert/remove,
 *  remounts or unmounts FatFs (re-running the SDIO init for a fresh
 *  card) and returns true so the caller can refresh its view.
 * ------------------------------------------------------------------ */
void loaderMediaInit(void);
bool loaderMediaSync(void);    /* true on the frame the presence changed */
bool loaderMediaPresent(void); /* debounced: is a card currently mounted? */

#endif /* __LOADER_MEDIA_H */
