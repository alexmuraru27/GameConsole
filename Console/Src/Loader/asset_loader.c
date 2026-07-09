#include "Loader/asset_loader.h"
#include "pak_format.h"
#include "Crc/crc.h"
#include "Logger/logger.h"
#include "ff.h"
#include <stddef.h>

/*
 * Asset loader: serves asset blobs out of the .pak bound to the running game.
 *
 * The game loader binds a pak with assetLoaderOpenPak() and the handle stays
 * open for the lifetime of the game, so lookups can seek straight into the file.
 * Games never see the pak — they ask for an asset by id and the loader resolves
 * the entry, copies the blob into their buffer, and verifies its CRC32.
 *
 * On-disk layout (pak_format.h): PakHeader (24B) | PakEntry[] | raw blobs.
 */

/* Entries scanned per f_read while resolving an id (256 B of stack). */
#define ENTRY_SCAN_CHUNK 16U

static FIL s_pak_file;
static bool s_is_pak_open = false;
static PakHeader s_pak_header; /* fixed 24-byte header only; entries stay on SD */

bool assetLoaderIsPakOpen(void)
{
    return s_is_pak_open;
}

void assetLoaderClosePak(void)
{
    if (s_is_pak_open)
    {
        f_close(&s_pak_file);
        s_is_pak_open = false;
        LOGGER_LOG_DEBUG(LOGGER_ASSETS, "pak closed");
    }
}

uint8_t assetLoaderOpenPak(const char *pak_filename)
{
    assetLoaderClosePak();

    FRESULT res = f_open(&s_pak_file, pak_filename, FA_READ);
    if (res == FR_NO_FILE || res == FR_NO_PATH)
    {
        /* A game may simply ship no assets — not an error, just nothing to bind. */
        LOGGER_LOG_INFO(LOGGER_ASSETS, "no pak '%s'", pak_filename);
        return ASSET_LOADER_RET_NO_PAK;
    }
    if (res != FR_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_ASSETS, "open '%s' failed (%d)", pak_filename, res);
        return ASSET_LOADER_RET_IO_ERROR;
    }

    UINT bytes_read;
    res = f_read(&s_pak_file, &s_pak_header, sizeof(PakHeader), &bytes_read);
    if (res != FR_OK || bytes_read != sizeof(PakHeader))
    {
        LOGGER_LOG_ERROR(LOGGER_ASSETS, "header read failed (%d)", res);
        f_close(&s_pak_file);
        return ASSET_LOADER_RET_IO_ERROR;
    }

    const uint32_t expected_data_offset = sizeof(PakHeader) + s_pak_header.assetCount * sizeof(PakEntry);
    if (s_pak_header.magic != PAK_MAGIC ||
        s_pak_header.version != PAK_VERSION ||
        s_pak_header.dataOffset != expected_data_offset ||
        s_pak_header.pakSize != f_size(&s_pak_file))
    {
        LOGGER_LOG_ERROR(LOGGER_ASSETS, "invalid pak '%s' (magic 0x%08lX v%lu)",
                         pak_filename, (unsigned long)s_pak_header.magic,
                         (unsigned long)s_pak_header.version);
        f_close(&s_pak_file);
        return ASSET_LOADER_RET_INVALID_PAK;
    }

    s_is_pak_open = true;
    LOGGER_LOG_INFO(LOGGER_ASSETS, "bound '%s' (%lu assets, %lu bytes)",
                    pak_filename, (unsigned long)s_pak_header.assetCount,
                    (unsigned long)s_pak_header.pakSize);
    return ASSET_LOADER_RET_OK;
}

/* Linear-scan the entry table for asset_id, reading it in fixed-size chunks. */
static uint8_t findEntry(uint32_t asset_id, PakEntry *const entry_out)
{
    FRESULT res = f_lseek(&s_pak_file, sizeof(PakHeader));
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_IO_ERROR;
    }

    PakEntry entries[ENTRY_SCAN_CHUNK];
    uint32_t remaining = s_pak_header.assetCount;
    while (remaining > 0U)
    {
        const uint32_t chunk = (remaining < ENTRY_SCAN_CHUNK) ? remaining : ENTRY_SCAN_CHUNK;
        const uint32_t chunk_bytes = chunk * sizeof(PakEntry);

        UINT bytes_read;
        res = f_read(&s_pak_file, entries, chunk_bytes, &bytes_read);
        if (res != FR_OK || bytes_read != chunk_bytes)
        {
            return ASSET_LOADER_RET_IO_ERROR;
        }

        for (uint32_t i = 0U; i < chunk; i++)
        {
            if (entries[i].id == asset_id)
            {
                *entry_out = entries[i];
                return ASSET_LOADER_RET_OK;
            }
        }

        remaining -= chunk;
    }

    return ASSET_LOADER_RET_ASSET_NOT_FOUND;
}

uint8_t assetLoaderGetAssetMetadata(uint32_t asset_id, AssetMetaData *asset_metadata_out)
{
    if (!s_is_pak_open)
    {
        return ASSET_LOADER_RET_NO_PAK;
    }
    if (asset_metadata_out == NULL)
    {
        return ASSET_LOADER_RET_ERR;
    }

    PakEntry entry;
    const uint8_t ret = findEntry(asset_id, &entry);
    if (ret != ASSET_LOADER_RET_OK)
    {
        return ret;
    }

    asset_metadata_out->id = entry.id;
    asset_metadata_out->size = entry.size;
    asset_metadata_out->crc32 = entry.crc32;
    return ASSET_LOADER_RET_OK;
}

uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *const buffer, const uint32_t buffer_size_bytes)
{
    if (!s_is_pak_open)
    {
        return ASSET_LOADER_RET_NO_PAK;
    }
    if (buffer == NULL)
    {
        return ASSET_LOADER_RET_ERR;
    }

    PakEntry entry;
    const uint8_t ret = findEntry(asset_id, &entry);
    if (ret != ASSET_LOADER_RET_OK)
    {
        LOGGER_LOG_WARN(LOGGER_ASSETS, "asset %lu not found", (unsigned long)asset_id);
        return ret;
    }

    if (buffer_size_bytes < entry.size)
    {
        LOGGER_LOG_WARN(LOGGER_ASSETS, "asset %lu needs %lu B, buffer is %lu B",
                        (unsigned long)asset_id, (unsigned long)entry.size,
                        (unsigned long)buffer_size_bytes);
        return ASSET_LOADER_RET_BUFFER_TOO_SMALL;
    }

    FRESULT res = f_lseek(&s_pak_file, s_pak_header.dataOffset + entry.offset);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_IO_ERROR;
    }

    UINT bytes_read;
    res = f_read(&s_pak_file, buffer, entry.size, &bytes_read);
    if (res != FR_OK || bytes_read != entry.size)
    {
        LOGGER_LOG_ERROR(LOGGER_ASSETS, "asset %lu read failed (%d)", (unsigned long)asset_id, res);
        return ASSET_LOADER_RET_IO_ERROR;
    }

    const uint32_t crc = crc32_calculate(buffer, entry.size);
    if (crc != entry.crc32)
    {
        LOGGER_LOG_ERROR(LOGGER_ASSETS, "asset %lu CRC 0x%08lX != 0x%08lX",
                         (unsigned long)asset_id, (unsigned long)crc,
                         (unsigned long)entry.crc32);
        return ASSET_LOADER_RET_CRC_MISMATCH;
    }

    LOGGER_LOG_DEBUG(LOGGER_ASSETS, "loaded asset %lu (%lu B)",
                     (unsigned long)asset_id, (unsigned long)entry.size);
    return ASSET_LOADER_RET_OK;
}
