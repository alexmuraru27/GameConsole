#include "asset_loader.h"
#include "loader.h"
#include "game_loader.h"
#include <string.h>

uint8_t assetLoaderGetAssetSize(uint32_t asset_id, uint32_t *buffer_size)
{
    if (!loaderIsFileOpened() || buffer_size == NULL)
    {
        return ASSET_LOADER_RET_ERR;
    }
    uint8_t res;
    GameHeader game_header;
    res = gameLoaderGetHeader(&game_header);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    AssetHeader asset_header;
    res = assetLoaderGetAssetHeader(&asset_header);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    uint32_t file_offset = (game_header.assets_start - game_header.header_start + sizeof(AssetHeader));
    res = f_lseek(loaderGetFile(), file_offset);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    uint32_t remaining_assets_count = asset_header.asset_count;
    // File pointer now pointing towards the first asset in the package
    while (remaining_assets_count > 0U)
    {
        AssetMetaData asset_metadata;
        UINT asset_metadata_bytes_read;
        // from AssetData offset, read only the first sizeof(AssetMetaData) bytes
        res = f_read(loaderGetFile(), &asset_metadata, sizeof(AssetMetaData), &asset_metadata_bytes_read);
        if (res != FR_OK || asset_metadata_bytes_read == 0U)
        {
            return res;
        }

        if (asset_metadata.id == asset_id)
        {
            *buffer_size = asset_metadata.size;
            return ASSET_LOADER_RET_OK;
        }
        else
        {
            // seek next AssetData position
            file_offset += sizeof(AssetMetaData) + asset_metadata.size;
            res = f_lseek(loaderGetFile(), file_offset);
            if (res != FR_OK)
            {
                return ASSET_LOADER_RET_ERR;
            }

            remaining_assets_count--;
        }
    }

    return ASSET_LOADER_RET_ASSET_NOT_FOUND;
}

uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *buffer)
{
    uint8_t res;
    GameHeader game_header;
    res = gameLoaderGetHeader(&game_header);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    AssetHeader asset_header;
    res = assetLoaderGetAssetHeader(&asset_header);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    uint32_t file_offset = (game_header.assets_start - game_header.header_start + sizeof(AssetHeader));
    res = f_lseek(loaderGetFile(), file_offset);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    uint32_t remaining_assets_count = asset_header.asset_count;
    // File pointer now pointing towards the first asset in the package
    while (remaining_assets_count > 0U)
    {
        AssetMetaData asset_metadata;
        UINT asset_metadata_bytes_read;
        // from AssetData offset, read only the first sizeof(AssetMetaData) bytes
        res = f_read(loaderGetFile(), &asset_metadata, sizeof(AssetMetaData), &asset_metadata_bytes_read);
        if (res != FR_OK || asset_metadata_bytes_read == 0U)
        {
            return res;
        }

        if (asset_metadata.id == asset_id)
        {
            uint8_t asset_read_buffer[128U];
            UINT bytes_read = 0U;
            uint32_t dest_addr = (uint32_t)buffer;
            uint32_t remaining_bytes = asset_metadata.size;
            while (remaining_bytes > 0U)
            {
                uint32_t bytes_to_read = (remaining_bytes > sizeof(asset_read_buffer)) ? sizeof(asset_read_buffer) : remaining_bytes;
                res = f_read(loaderGetFile(), asset_read_buffer, bytes_to_read, &bytes_read);
                if (res != FR_OK || bytes_read == 0)
                {
                    break;
                }

                memcpy((void *)dest_addr, asset_read_buffer, bytes_read);

                remaining_bytes -= bytes_read;
                dest_addr += bytes_read;

                // read less than requested, end of file - RETURN ok
                if (bytes_read <= bytes_to_read)
                {
                    return ASSET_LOADER_RET_OK;
                }
            }

            if (remaining_bytes > 0U)
            {
                return ASSET_LOADER_RET_ERR;
            }
        }
        else
        {
            // seek next AssetData position
            file_offset += sizeof(AssetMetaData) + asset_metadata.size;
            res = f_lseek(loaderGetFile(), file_offset);
            if (res != FR_OK)
            {
                return ASSET_LOADER_RET_ERR;
            }

            remaining_assets_count--;
        }
    }
    return ASSET_LOADER_RET_ASSET_NOT_FOUND;
}

uint8_t assetLoaderGetAssetHeader(AssetHeader *asset_header)
{
    if (!loaderIsFileOpened() || asset_header == NULL)
    {
        return ASSET_LOADER_RET_ERR;
    }

    FRESULT res;
    UINT assets_header_bytes_read;
    uint8_t assets_header_buffer[sizeof(AssetHeader)];
    GameHeader game_header;
    res = gameLoaderGetHeader(&game_header);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    res = f_lseek(loaderGetFile(), game_header.assets_start - game_header.header_start);
    if (res != FR_OK)
    {
        return ASSET_LOADER_RET_ERR;
    }

    res = f_read(loaderGetFile(), assets_header_buffer, sizeof(AssetHeader), &assets_header_bytes_read);
    if (res != FR_OK || assets_header_bytes_read == 0U)
    {
        return res;
    }
    memcpy(asset_header, assets_header_buffer, sizeof(AssetHeader));
    return ASSET_LOADER_RET_OK;
}