#ifndef __ASSET_LOADER_H
#define __ASSET_LOADER_H

#include "stdint.h"
#include "stdbool.h"
#include "game_console_api.h"

/* Return codes shared by every asset-loader entry point. */
#define ASSET_LOADER_RET_OK 0U                /* success */
#define ASSET_LOADER_RET_ERR 1U               /* generic / unexpected error */
#define ASSET_LOADER_RET_ASSET_NOT_FOUND 2U   /* no entry with the requested id */
#define ASSET_LOADER_RET_NO_PAK 3U            /* no pak is currently bound */
#define ASSET_LOADER_RET_BUFFER_TOO_SMALL 4U  /* caller buffer < asset size */
#define ASSET_LOADER_RET_CRC_MISMATCH 5U      /* blob CRC32 did not match the entry */
#define ASSET_LOADER_RET_IO_ERROR 6U          /* SD / FatFs read error */
#define ASSET_LOADER_RET_INVALID_PAK 7U       /* magic / version / sizes inconsistent */

/*
 * Pak lifecycle (console-internal, driven by the game loader).
 *
 * A single pak is bound at a time: the one belonging to the game currently
 * running. The loader opens it when the game is loaded and keeps the file handle
 * open so asset reads can seek into it on demand, then unbinds it when the game
 * exits. assetLoaderOpenPak returns ASSET_LOADER_RET_NO_PAK when the file simply
 * does not exist (a game shipping no assets) — that is not a hard error.
 */
uint8_t assetLoaderOpenPak(const char *pak_filename);
void assetLoaderClosePak(void);
bool assetLoaderIsPakOpen(void);

/*
 * Game-facing API, exposed through the ConsoleAPI table. Both operate on the
 * currently bound pak and return ASSET_LOADER_RET_NO_PAK if none is bound.
 */
uint8_t assetLoaderGetAssetMetadata(uint32_t asset_id, AssetMetaData *asset_metadata_out);
uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *const buffer, const uint32_t buffer_size_bytes);

#endif /* __ASSET_LOADER_H */
