#ifndef _DISKIO_INTEGRATION_DEFINED
#define _DISKIO_INTEGRATION_DEFINED

#define DRIVE_SD 0
#include <stdint.h>
#include "sdio.h"

#define SD_CARD_SDSC 0U
#define SD_CARD_SDHC 1U

/* SD block geometry — one source for the low-level driver, the FatFs glue's
 * read-back verify buffer, and the ioctl sector-size reply. */
#define SD_BLOCK_SIZE      512U
#define SD_BLOCK_SIZE_LOG2 9U /* log2(SD_BLOCK_SIZE); the SDIO DCTRL DBLOCKSIZE field */

/* Fallback logical-sector counts used when the card's CSD can't be read; shared
 * by getSdSectorCount() and the GET_SECTOR_COUNT ioctl so they agree. */
#define SD_DEFAULT_SECTORS_SDSC 0x100000UL  /* ~512 MB */
#define SD_DEFAULT_SECTORS_SDHC 0x3B00000UL /* ~30 GB */

uint8_t sdInit(void);
uint8_t sdReadSingleBlock(uint32_t block_addr, uint8_t *buffer);
uint8_t sdReadMultipleBlocks(uint32_t block_addr, uint8_t *buffer, uint32_t count);
uint8_t sdWriteSingleBlock(uint32_t block_addr, const uint8_t *buffer);
uint8_t sdWriteMultipleBlocks(uint32_t block_addr, const uint8_t *buffer, uint32_t count);

uint8_t getSdType(void);
uint32_t getSdSectorCount(void);
void sdWaitCardReady(void);

/* Reset the cached "card initialized" flag so the next mount/access re-runs the
 * SDIO init handshake — used to recover after a card is swapped at runtime. */
void diskMarkUninitialized(void);

#endif
