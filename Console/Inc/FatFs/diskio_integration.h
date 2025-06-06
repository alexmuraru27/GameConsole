#ifndef _DISKIO_INTEGRATION_DEFINED
#define _DISKIO_INTEGRATION_DEFINED

#define DRIVE_SD 0
#include <stdint.h>
#include "sdio.h"

#define SD_CARD_SDSC 0U
#define SD_CARD_SDHC 1U

uint8_t sdInit(void);
uint8_t sdReadSingleBlock(uint32_t block_addr, uint8_t *buffer);
uint8_t sdReadMultipleBlocks(uint32_t block_addr, uint8_t *buffer, uint32_t count);

uint8_t getSdType();

#endif
