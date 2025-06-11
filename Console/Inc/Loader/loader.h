#ifndef __LOADER_H
#define __LOADER_H
#include "stdint.h"
#include "ff.h"

uint32_t loaderGetMaxFilenameSize(void);
uint32_t loaderGetBinaryFilesNumberInDirectory();
FRESULT loaderOpenBinaryFileByIndex(uint32_t index, FIL *file);
FRESULT loaderGetBinaryFilenameByIndex(uint32_t index, char *filename_out, uint32_t *filename_length);

#endif /* __LOADER_H */
