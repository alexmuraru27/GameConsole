
#ifndef __ASSET_API_H
#define __ASSET_API_H
#include <stdint.h>
#include "stdbool.h"

/*
 * Game-facing view of an asset pak. These mirror the relevant fields of the
 * on-disk .pak format (tools/packer/asset_format.h), which the console parses on
 * the game's behalf — games never read the SD card or the raw pak directly.
 */

/* Summary of the .pak bound to the running game (from its PakHeader). */
typedef struct
{
    char magic[4];        /* "PAK1" */
    uint32_t version;     /* pak format version */
    uint32_t asset_count; /* number of assets in the pak */
} __attribute__((packed, aligned(1))) AssetHeader;

/* Per-asset descriptor (from the matching PakEntry). */
typedef struct
{
    uint32_t id;    /* asset id (matches a generated <name>AssetId enum value) */
    uint32_t size;  /* asset size in bytes — the buffer needed to load it */
    uint32_t crc32; /* CRC32 of the asset blob, verified on load */
} __attribute__((packed, aligned(1))) AssetMetaData;

#endif /* __ASSET_API_H */