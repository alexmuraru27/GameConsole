#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>
#include "ff.h"
#include "diskio.h"
#include "diskio_integration.h"
#include "sysclock.h"

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

    uint8_t ret;
    uint32_t retry_count = 0;

    do
    {
        sdWaitCardReady();

        if (count == 1)
        {
            ret = sdWriteSingleBlock(sector, buff);
        }
        else
        {
            ret = sdWriteMultipleBlocks(sector, buff, count);
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
        *(LBA_t *)buff = c ? c : ((getSdType() == SD_CARD_SDHC) ? 0x3B00000UL : 0x100000UL);
    }
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
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
