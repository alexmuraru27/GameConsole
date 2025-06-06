#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>
#include "ff.h"
#include "diskio.h"
#include "diskio_integration.h"
#include "sysclock.h"

#define MAX_RETRIES 3
uint8_t s_card_initialized = 0;

/*-----------------------------------------------------------------------*/
/* FatFs Media Access Interface Implementation                           */
/*-----------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                     */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
	if (pdrv != DRIVE_SD)
	{
		return STA_NOINIT; // Invalid drive
	}

	if (!s_card_initialized)
	{
		return STA_NOINIT; // Not initialized
	}

	return 0; // OK
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                   */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
	if (pdrv != DRIVE_SD)
	{
		return STA_NOINIT; // Invalid drive
	}

	// Check if already initialized
	if (s_card_initialized)
	{
		return 0; // Already initialized
	}

	// Initialize SD card
	if (sdInit() != SD_OK)
	{
		return STA_NOINIT;
	}
	s_card_initialized = 1U;

	return 0; // OK
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	if (pdrv != DRIVE_SD)
	{
		return RES_PARERR; // Invalid drive
	}

	if (!s_card_initialized)
	{
		return RES_NOTRDY; // Not initialized
	}

	if (!count)
	{
		return RES_PARERR; // Invalid parameter
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
		delay(10); // Small delay before retry

	} while (retry_count < MAX_RETRIES);
	return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                      */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	if (pdrv != DRIVE_SD)
	{
		return RES_PARERR; // Invalid drive
	}

	if (!s_card_initialized)
	{
		return RES_NOTRDY; // Not initialized
	}

	if (!count)
	{
		return RES_PARERR; // Invalid parameter
	}

	uint8_t ret;

	if (count == 1)
	{
		// TODO at some point when i want some writing
		ret = RES_PARERR;
	}
	else
	{
		// TODO at some point when i want some writing
		ret = RES_PARERR;
	}

	if (ret == SD_OK)
	{
		return RES_OK;
	}

	delay(10); // Small delay before retry
	return RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	if (pdrv != DRIVE_SD)
	{
		return RES_PARERR; // Invalid drive
	}

	if (!s_card_initialized)
	{
		return RES_NOTRDY; // Not initialized
	}

	switch (cmd)
	{
	case CTRL_SYNC:
		// Complete pending write process
		// For our implementation, writes are synchronous
		return RES_OK;

	case GET_SECTOR_COUNT:
		// Get number of sectors on the disk
		// This would require reading CSD register for actual implementation
		// For now, return a large number for SDHC cards
		if (getSdType() == SD_CARD_SDHC)
		{
			*(LBA_t *)buff = 0x1000000; // 8GB worth of sectors (approximate)
		}
		else
		{
			*(LBA_t *)buff = 0x100000; // 512MB worth of sectors (approximate)
		}
		return RES_OK;

	case GET_SECTOR_SIZE:
		// Get sector size
		*(WORD *)buff = 512;
		return RES_OK;

	case GET_BLOCK_SIZE:
		// Get erase block size (for flash memory)
		*(DWORD *)buff = 1; // Single sector erase
		return RES_OK;

	default:
		return RES_PARERR;
	}
}

/*-----------------------------------------------------------------------*/
/* Get current time for file timestamps                                 */
/*-----------------------------------------------------------------------*/
DWORD get_fattime(void)
{
	// Return a fixed time for now
	// Format: bit31:25=Year(0-127 org.1980), bit24:21=Month(1-12), bit20:16=Day(1-31)
	//         bit15:11=Hour(0-23), bit10:5=Minute(0-59), bit4:0=Second(0-29)*2

	// Example: 2024-06-07 12:30:00
	return ((2024 - 1980) << 25) | (6 << 21) | (7 << 16) | (12 << 11) | (30 << 5) | (0 << 0);
}