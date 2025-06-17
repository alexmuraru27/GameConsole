#ifndef __ASSET_LOADER_H
#define __ASSET_LOADER_H

#include "stdint.h"
#include "game_console_api.h"

#define ASSET_LOADER_RET_OK 0U
#define ASSET_LOADER_RET_ERR 1U
#define ASSET_LOADER_RET_ASSET_NOT_FOUND 2U

uint8_t assetLoaderGetAssetSize(uint32_t asset_id, uint32_t *buffer_size);
uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *buffer);
uint8_t assetLoaderGetAssetHeader(AssetHeader *asset_header);

#endif /* __ASSET_LOADER_H */
