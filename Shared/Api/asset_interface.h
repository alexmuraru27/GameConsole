
#ifndef __ASSET_API_H
#define __ASSET_API_H
#include <stdint.h>
#include "stdbool.h"

/*
 * Game-facing view of an asset pak. This mirrors the relevant fields of the
 * on-disk .pak format (tools/packer/pak_format.h), which the console parses on
 * the game's behalf — games never read the SD card or the raw pak directly.
 */

/* Per-asset descriptor (from the matching PakEntry). */
typedef struct
{
    uint32_t id;    /* asset id (matches a generated <name>AssetId enum value) */
    uint32_t size;  /* asset size in bytes — the buffer needed to load it */
    uint32_t crc32; /* CRC32 of the asset blob, verified on load */
} __attribute__((packed, aligned(1))) AssetMetaData;

/*
 * The CCM "asset arena": a ~64 KB scratch region (the GAME_RAM_ASSET CCM bank) a
 * game may freely use to stage assets it streams from its pak — or any large
 * game-owned data. Its bounds are fixed by the game linker script, which emits
 * these symbols over the .asset_area section (linker/app.ld). The macros give a
 * game typed access to the region without re-declaring the linker symbols. The
 * symbols exist only in the game link; the console never references them.
 */
extern uint8_t __asset_area_start[];
extern uint8_t __asset_area_end[];

#define ASSET_ARENA_START ((uint8_t *)__asset_area_start)
#define ASSET_ARENA_END   ((uint8_t *)__asset_area_end)
#define ASSET_ARENA_SIZE  ((uint32_t)(ASSET_ARENA_END - ASSET_ARENA_START))

#endif /* __ASSET_API_H */