#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>
#include "ff.h"
#include "diskio.h"
#include "diskio_integration.h"
#include "Peripherals/sysclock.h"
#include "Logger/logger.h"

#define MAX_RETRIES 3
static uint8_t s_card_initialized = 0;

/* Force the card to be re-initialized on the next disk_initialize(). Called by
 * the loader when the card is removed or (re)inserted, so a freshly inserted
 * card runs the full SDIO init handshake again instead of reusing stale state. */
void diskMarkUninitialized(void)
{
    s_card_initialized = 0U;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != DRIVE_SD)
    {
        return STA_NOINIT;
    }
    if (!s_card_initialized)
    {
        return STA_NOINIT;
    }
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != DRIVE_SD)
    {
        return STA_NOINIT;
    }
    if (s_card_initialized)
    {
        return 0;
    }
    if (sdInit() != SD_OK)
    {
        return STA_NOINIT;
    }
    s_card_initialized = 1U;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != DRIVE_SD)
    {
        return RES_PARERR;
    }
    if (!s_card_initialized)
    {
        return RES_NOTRDY;
    }
    if (!count)
    {
        return RES_PARERR;
    }

    uint8_t ret;
    uint32_t retry_count = 0;

    do
    {
        if (count == 1)
        {
            ret = sdReadSingleBlock(sector, buff);
        }
        else
        {
            ret = sdReadMultipleBlocks(sector, buff, count);
        }

        if (ret == SD_OK)
        {
            return RES_OK;
        }

        retry_count++;
        sdWaitCardReady();
    } while (retry_count < MAX_RETRIES);

    return RES_ERROR;
}

/* Read each just-written block back and compare to the source. A flaky card can
 * report a write OK yet store wrong/incomplete bytes; this catches that so the
 * write can be retried. A single 512 B scratch handles any `count`. */
static bool verifyWrite(const BYTE *buff, LBA_t sector, UINT count)
{
    static uint8_t s_verify[SD_BLOCK_SIZE];
    sdWaitCardReady(); /* programming must finish before reading back */
    for (UINT i = 0; i < count; i++)
    {
        if (sdReadSingleBlock((uint32_t)(sector + i), s_verify) != SD_OK)
        {
            return false;
        }
        if (memcmp(s_verify, buff + ((uint32_t)i * SD_BLOCK_SIZE), SD_BLOCK_SIZE) != 0)
        {
            return false;
        }
    }
    return true;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != DRIVE_SD)
    {
        return RES_PARERR;
    }
    if (!s_card_initialized)
    {
        return RES_NOTRDY;
    }
    if (!count)
    {
        return RES_PARERR;
    }

    uint32_t retry_count = 0;

    do
    {
        sdWaitCardReady();

        const uint8_t ret = (count == 1) ? sdWriteSingleBlock(sector, buff)
                                         : sdWriteMultipleBlocks(sector, buff, count);

        /* Accept the write only if it both reported OK and reads back identical. */
        if (ret == SD_OK && verifyWrite(buff, sector, count))
        {
            return RES_OK;
        }

        LOGGER_LOG_WARN(LOGGER_SDIO, "write sector %lu x%u: %s (retry %lu/%d)",
                        (unsigned long)sector, (unsigned)count,
                        (ret == SD_OK) ? "verify mismatch" : "write error",
                        (unsigned long)(retry_count + 1U), MAX_RETRIES);
        retry_count++;
        sdWaitCardReady();
    } while (retry_count < MAX_RETRIES);

    LOGGER_LOG_ERROR(LOGGER_SDIO, "write sector %lu x%u failed after %d retries",
                     (unsigned long)sector, (unsigned)count, MAX_RETRIES);
    return RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != DRIVE_SD)
    {
        return RES_PARERR;
    }
    if (!s_card_initialized)
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
    {
        uint32_t c = getSdSectorCount();
        *(LBA_t *)buff = c ? c : ((getSdType() == SD_CARD_SDHC) ? SD_DEFAULT_SECTORS_SDHC : SD_DEFAULT_SECTORS_SDSC);
    }
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SD_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    return ((2024 - 1980) << 25) | (6 << 21) | (7 << 16) | (12 << 11) | (30 << 5) | (0 << 0);
}
