#ifndef __LOADER_H
#define __LOADER_H
#include <stdint.h>
#include "ff.h"
#include <stdbool.h>

FRESULT loaderOpenFile(uint32_t binary_index);
FIL *loaderGetFile(void);
FILINFO *loaderGetFileInfo(void);
FRESULT loaderCloseFile(void);
bool loaderIsFileOpened(void);
uint32_t loaderGetMaxFilenameSize(void);
uint32_t loaderGetBinaryFilesNumberInDirectory(void);
FRESULT loaderGetFilenameByIndex(uint32_t binary_index, char *filename_out, uint32_t *filename_length);

/* Delete game #binary_index: removes Games/<name>.bin and, if present, its paired
 * Games/<name>.pak. Returns the .bin unlink result (FR_OK on success); the .pak is
 * best-effort, since a game may ship without one. */
FRESULT loaderDeleteGame(uint32_t binary_index);

/* SD-card mount/hotplug lives in loader_media.h; the crash-log sink in crash_log.h. */

#endif /* __LOADER_H */
