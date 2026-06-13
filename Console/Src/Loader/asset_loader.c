#include "asset_loader.h"
#include "loader.h"
#include "game_loader.h"
#include <string.h>

uint8_t assetLoaderGetAssetMetadata(uint32_t asset_id, AssetMetaData *asset_metadata_out)
{
    // TODO read asset from pkg file
    return ASSET_LOADER_RET_ASSET_NOT_FOUND;
}

uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *const buffer, const uint8_t buffer_size_bytes)
{
    // TODO read asset from pkg file
    return ASSET_LOADER_RET_ASSET_NOT_FOUND;
}

uint8_t assetLoaderGetAssetHeader(AssetHeader *asset_header)
{
    // TODO read asset from pkg file
    return ASSET_LOADER_RET_OK;
}