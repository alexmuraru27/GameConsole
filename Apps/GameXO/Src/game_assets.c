#include "game_assets.h"
#include "gfx_asset.h"     /* GfxAssetHeader / GFX1 format (tools/graphics) */
#include "music_track.h"   /* MusicTrackHeader / NOT1 format (tools/music_creator) */
#include <string.h>


/* The CCM asset arena (ASSET_ARENA_START..END, the .asset_area over GAME_RAM_ASSET,
 * exposed by the ConsoleAPI). Graphics are bump-allocated here; it is never reset
 * after the one-time load. */
static uint8_t *s_arena;

void gameAssetsInit(void)
{
    s_arena = ASSET_ARENA_START;
}

static uint8_t *arenaAlloc(uint32_t size)
{
    uint8_t *p = (uint8_t *)(((uintptr_t)s_arena + 3U) & ~(uintptr_t)3U);
    if (p + size > ASSET_ARENA_END)
    {
        return NULL;
    }
    s_arena = p + size;
    return p;
}

bool gameAssetsLoadSprite(uint32_t asset_id, Sprite *sprite_out, uint16_t *palette_out)
{
    AssetMetaData meta;
    if (assetLoaderGetAssetMetadata(asset_id, &meta) != 0U)
    {
        return false;
    }

    uint8_t *blob = arenaAlloc(meta.size);
    if (blob == NULL)
    {
        return false;
    }
    if (assetLoaderGetAssetData(asset_id, blob, meta.size) != 0U)
    {
        return false;
    }

    const GfxAssetHeader *header = (const GfxAssetHeader *)blob;
    if (header->magic != GFX_ASSET_MAGIC)
    {
        return false;
    }

    /* GfxAsset pixel layout already matches the renderer's, and its flags byte
     * mirrors the low SpriteFlags bits (format + opacity). Only the palette,
     * stored as system-palette indices, needs mapping to RGB565. */
    const uint8_t *pixels = blob + sizeof(GfxAssetHeader);
    const uint8_t *index_palette = pixels + header->dataSize;
    const uint32_t colors = (header->format == GFX_FMT_4BPP) ? 16U : 4U;
    for (uint32_t i = 0U; i < colors; i++)
    {
        palette_out[i] = rendererSystemColor(index_palette[i]);
    }

    *sprite_out = (Sprite){
        .x = 0, .y = 0,
        .w = (uint16_t)header->width,
        .h = (uint16_t)header->height,
        .z = 0U,
        .flags = (uint8_t)header->flags,
        .pixels = pixels,
        .palette = palette_out,
    };
    return true;
}

/* Persistent track buffer: the buzzer keeps the notes pointer while playing, so
 * it must outlive the call (4-aligned so the uint16 note pairs are aligned). */
#define SOUND_BUFFER_BYTES 256U
static uint8_t s_sound_buffer[SOUND_BUFFER_BYTES] __attribute__((aligned(4)));

bool gameAssetsPlaySound(uint32_t asset_id)
{
    AssetMetaData meta;
    if (assetLoaderGetAssetMetadata(asset_id, &meta) != 0U)
    {
        return false;
    }
    if (meta.size > sizeof(s_sound_buffer))
    {
        return false;
    }
    if (assetLoaderGetAssetData(asset_id, s_sound_buffer, meta.size) != 0U)
    {
        return false;
    }

    const MusicTrackHeader *header = (const MusicTrackHeader *)s_sound_buffer;
    if (header->magic != NOTE_ASSET_MAGIC)
    {
        return false;
    }

    const uint16_t *notes = (const uint16_t *)(s_sound_buffer + sizeof(MusicTrackHeader));
    buzzerStopAll();
    buzzerPlay(0U, false, notes, (uint16_t)header->noteCount);
    return true;
}

uint16_t gameAssetsTextWidth(FontSize size, const char *text)
{
    const uint16_t advance = (uint16_t)(fontGlyphW(size) + 1U);
    const uint16_t n = (uint16_t)strlen(text);
    return (n == 0U) ? 0U : (uint16_t)(n * advance - 1U);
}
