#ifndef __LOADER_H
#define __LOADER_H
#include "stdint.h"
#include "ff.h"
#include "stdbool.h"

FRESULT loaderOpenFile(uint32_t binary_index);
FIL *loaderGetFile();
FILINFO *loaderGetFileInfo();
FRESULT loaderCloseFile();
bool loaderIsFileOpened();
uint32_t loaderGetMaxFilenameSize(void);
uint32_t loaderGetBinaryFilesNumberInDirectory();
FRESULT loaderGetFilenameByIndex(uint32_t binary_index, char *filename_out, uint32_t *filename_length);

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

#endif /* __LOADER_H */
