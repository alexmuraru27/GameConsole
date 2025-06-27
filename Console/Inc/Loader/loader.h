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

#endif /* __LOADER_H */
