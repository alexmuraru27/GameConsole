#include "os_flasher.h"
#include "flash_ll.h"
#include "flash_map.h"
#include "crc.h"
#include "game_console.h"
#include "logger.h"
#include "ff.h"
#include <string.h>

/*
 * Application side of OS self-flashing. We write only the staging region (sectors
 * 6-7) — never the running app — so the long, SD-dependent transfer can fail
 * (card yanked, power lost) with the live OS intact. We verify the staged copy by
 * readback before it can ever be committed. The committed header is what the
 * bootloader acts on after a reboot; see flash_map.h and Bootloader/.
 */

/* SD read granularity for streaming into staging. Word-multiple so every chunk
 * but the last programs whole words at a word-aligned offset. */
#define STAGE_CHUNK 4096U
static uint8_t s_buf[STAGE_CHUNK];

const char *osFlasherStatusString(OsFlashStatus status)
{
    switch (status)
    {
    case OS_FLASH_OK:
        return "OK";
    case OS_FLASH_NO_FILE:
        return "image not found";
    case OS_FLASH_TOO_BIG:
        return "image too large";
    case OS_FLASH_READ_FAIL:
        return "SD read error";
    case OS_FLASH_WRITE_FAIL:
        return "flash write error";
    case OS_FLASH_VERIFY_FAIL:
        return "verify failed";
    default:
        return "unknown error";
    }
}

/* Program `n` bytes to `flash_addr` (word-aligned), padding a final partial word
 * with 0xFF (the erased value, so it programs cleanly). Returns false on the first
 * word that fails to take. */
static bool programChunk(uint32_t flash_addr, const uint8_t *data, uint32_t n)
{
    for (uint32_t i = 0U; i < n; i += 4U)
    {
        uint32_t word = 0xFFFFFFFFU;
        const uint32_t take = (n - i >= 4U) ? 4U : (n - i);
        memcpy(&word, data + i, take); /* little-endian; unused high bytes stay 0xFF */
        if (!flashLlProgramWord(flash_addr + i, word))
        {
            return false;
        }
    }
    return true;
}

OsFlashStatus osFlasherStage(const char *path, uint32_t *out_crc, uint32_t *out_size,
                             OsFlashProgressCb cb, void *ctx)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "OS image '%s' not openable", path);
        return OS_FLASH_NO_FILE;
    }

    const uint32_t total = (uint32_t)f_size(&file);
    if (total == 0U)
    {
        f_close(&file);
        return OS_FLASH_NO_FILE;
    }
    if (total > OS_IMAGE_MAX_SIZE)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "OS image %lu B exceeds app region %lu B",
                         (unsigned long)total, (unsigned long)OS_IMAGE_MAX_SIZE);
        f_close(&file);
        return OS_FLASH_TOO_BIG;
    }

    LOGGER_LOG_INFO(LOGGER_FLASHER, "staging OS image '%s' (%lu B)", path, (unsigned long)total);

    flashLlUnlock();

    /* Wipe the whole staging region (header + image area) before writing. */
    OsFlashStatus status = OS_FLASH_OK;
    LOGGER_LOG_INFO(LOGGER_FLASHER, "erasing staging sectors %lu..%lu",
                    (unsigned long)STAGING_SECTOR_FIRST, (unsigned long)STAGING_SECTOR_LAST);
    for (uint32_t s = STAGING_SECTOR_FIRST; s <= STAGING_SECTOR_LAST; s++)
    {
        if (!flashLlEraseSector(s))
        {
            LOGGER_LOG_ERROR(LOGGER_FLASHER, "staging sector %lu erase failed", (unsigned long)s);
            status = OS_FLASH_WRITE_FAIL;
            break;
        }
        LOGGER_LOG_DEBUG(LOGGER_FLASHER, "  erased staging sector %lu", (unsigned long)s);
    }

    if (status == OS_FLASH_OK)
    {
        LOGGER_LOG_INFO(LOGGER_FLASHER, "streaming image -> staging 0x%08lX (%u B chunks)",
                        (unsigned long)STAGING_IMAGE_ADDR, (unsigned)STAGE_CHUNK);
    }

    uint32_t crc = CRC32_INIT;
    uint32_t done = 0U;
    while (status == OS_FLASH_OK && done < total)
    {
        UINT got = 0U;
        if (f_read(&file, s_buf, STAGE_CHUNK, &got) != FR_OK)
        {
            status = OS_FLASH_READ_FAIL;
            break;
        }
        if (got == 0U)
        {
            status = OS_FLASH_READ_FAIL; /* short file vs. f_size */
            break;
        }

        if (!programChunk(STAGING_IMAGE_ADDR + done, s_buf, got))
        {
            LOGGER_LOG_ERROR(LOGGER_FLASHER, "program failed at offset %lu", (unsigned long)done);
            status = OS_FLASH_WRITE_FAIL;
            break;
        }

        crc = crc32_update(crc, s_buf, got);
        done += got;
        LOGGER_LOG_DEBUG(LOGGER_FLASHER, "  staged %lu/%lu B (%lu%%)",
                         (unsigned long)done, (unsigned long)total, (unsigned long)(done * 100U / total));
        if (cb != NULL)
        {
            cb(done, total, ctx);
        }
    }

    flashLlLock();
    f_close(&file);

    if (status != OS_FLASH_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "staging failed: %s", osFlasherStatusString(status));
        return status;
    }

    /* Read the staged image back and confirm the flash holds exactly what we read
     * off the card. This catches a bad program; the caller separately checks the
     * read CRC against the expected image to catch a bad read. */
    LOGGER_LOG_INFO(LOGGER_FLASHER, "stream done (%lu B); verifying staging by readback", (unsigned long)done);
    const uint32_t read_crc = crc32_final(crc);
    const uint32_t staged_crc = crc32_calculate((const uint8_t *)STAGING_IMAGE_ADDR, total);
    if (staged_crc != read_crc)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "staging readback mismatch: wrote %08lX read %08lX",
                         (unsigned long)read_crc, (unsigned long)staged_crc);
        return OS_FLASH_VERIFY_FAIL;
    }

    LOGGER_LOG_INFO(LOGGER_FLASHER, "staged + verified (%lu B, crc %08lX)",
                    (unsigned long)total, (unsigned long)read_crc);
    *out_crc = read_crc;
    *out_size = total;
    return OS_FLASH_OK;
}

OsFlashStatus osFlasherCommitAndReboot(uint32_t image_size, uint32_t image_crc32)
{
    OsStagingHeader hdr;
    hdr.magic = OS_STAGING_MAGIC;
    hdr.image_size = image_size;
    hdr.image_crc32 = image_crc32;
    hdr.header_crc32 = crc32_calculate((const uint8_t *)&hdr, OS_STAGING_HEADER_CRC_LEN);

    const uint32_t base = STAGING_FLASH_ADDR + STAGING_HEADER_OFFSET;

    LOGGER_LOG_INFO(LOGGER_FLASHER, "committing staging header @ 0x%08lX (size=%lu, img_crc=%08lX, hdr_crc=%08lX)",
                    (unsigned long)base, (unsigned long)image_size,
                    (unsigned long)image_crc32, (unsigned long)hdr.header_crc32);

    flashLlUnlock();
    /* Write the body first, then the magic last: a torn write leaves magic absent,
     * which the bootloader reads as "no pending update" (old OS boots untouched). */
    bool ok = flashLlProgramWord(base + 4U, hdr.image_size);
    ok = ok && flashLlProgramWord(base + 8U, hdr.image_crc32);
    ok = ok && flashLlProgramWord(base + 12U, hdr.header_crc32);
    ok = ok && flashLlProgramWord(base + 0U, hdr.magic);
    flashLlLock();

    const OsStagingHeader *rb = (const OsStagingHeader *)base;
    if (!ok || rb->magic != hdr.magic || rb->image_size != hdr.image_size ||
        rb->image_crc32 != hdr.image_crc32 || rb->header_crc32 != hdr.header_crc32)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "staging header write failed");
        return OS_FLASH_WRITE_FAIL;
    }

    LOGGER_LOG_INFO(LOGGER_FLASHER, "OS update committed (%lu B, crc %08lX); rebooting to apply",
                    (unsigned long)image_size, (unsigned long)image_crc32);
    gameConsoleReboot(); /* into the bootloader, which applies + verifies; does not return */
    return OS_FLASH_OK;  /* unreachable */
}
