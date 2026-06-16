#include "game_assets.h"
#include "gfx_asset.h"     /* GfxAssetHeader / GFX1 format (tools/graphics) */
#include "music_track.h"   /* MusicTrackHeader / NOT1 format (tools/music_creator) */
#include <string.h>

DECLARE_API_HEADER_PTR(s_api);
#define API (s_api->api)

/* The CCM asset arena reserved by game.ld (.asset_area over GAME_RAM_ASSET).
 * Graphics are bump-allocated here; it is never reset after the one-time load. */
extern uint8_t __asset_area_start[];
extern uint8_t __asset_area_end[];
static uint8_t *s_arena;

void gameAssetsInit(void)
{
    s_arena = __asset_area_start;
}

static uint8_t *arenaAlloc(uint32_t size)
{
    uint8_t *p = (uint8_t *)(((uintptr_t)s_arena + 3U) & ~(uintptr_t)3U);
    if (p + size > __asset_area_end)
    {
        return NULL;
    }
    s_arena = p + size;
    return p;
}

bool gameAssetsLoadSprite(uint32_t asset_id, Sprite *sprite_out, uint16_t *palette_out)
{
    AssetMetaData meta;
    if (API.assetLoaderGetAssetMetadata(asset_id, &meta) != 0U)
    {
        return false;
    }

    uint8_t *blob = arenaAlloc(meta.size);
    if (blob == NULL)
    {
        return false;
    }
    if (API.assetLoaderGetAssetData(asset_id, blob, meta.size) != 0U)
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
        palette_out[i] = API.rendererSystemColor(index_palette[i]);
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
    if (API.assetLoaderGetAssetMetadata(asset_id, &meta) != 0U)
    {
        return false;
    }
    if (meta.size > sizeof(s_sound_buffer))
    {
        return false;
    }
    if (API.assetLoaderGetAssetData(asset_id, s_sound_buffer, meta.size) != 0U)
    {
        return false;
    }

    const MusicTrackHeader *header = (const MusicTrackHeader *)s_sound_buffer;
    if (header->magic != NOTE_ASSET_MAGIC)
    {
        return false;
    }

    const uint16_t *notes = (const uint16_t *)(s_sound_buffer + sizeof(MusicTrackHeader));
    API.buzzerStopAll();
    API.buzzerPlay(0U, false, notes, (uint16_t)header->noteCount);
    return true;
}

uint16_t gameAssetsTextWidth(FontSize size, const char *text)
{
    const uint16_t advance = (uint16_t)(API.fontGlyphW(size) + 1U);
    const uint16_t n = (uint16_t)strlen(text);
    return (n == 0U) ? 0U : (uint16_t)(n * advance - 1U);
}

uint16_t gameAssetsDrawText(Sprite *out, uint16_t max, FontSize size,
                            int16_t x, int16_t y, uint8_t z,
                            const uint16_t *palette, const char *text)
{
    const uint16_t gw = API.fontGlyphW(size);
    const uint16_t gh = API.fontGlyphH(size);
    uint16_t count = 0U;

    for (const char *scan = text; *scan != '\0' && count < max; scan++)
    {
        const uint8_t ascii = (uint8_t)*scan;
        if (ascii >= 0x20U && ascii <= 0x7EU)
        {
            const uint8_t *pixels;
            API.fontGet(ascii, size, &pixels);
            out[count++] = (Sprite){.x = x, .y = y, .w = gw, .h = gh, .z = z,
                                    .flags = 0U, .pixels = pixels, .palette = palette};
        }
        x = (int16_t)(x + gw + 1U);
    }
    return count;
}

uint16_t gameAssetsDrawTextScaled(Sprite *out, uint16_t max, FontSize size,
                                  int16_t x, int16_t y, uint8_t z, uint8_t factor,
                                  const uint16_t *palette, const char *text,
                                  uint8_t *pool, uint16_t pool_size)
{
    const uint8_t sw = (uint8_t)(API.fontGlyphW(size) * factor);
    const uint8_t sh = (uint8_t)(API.fontGlyphH(size) * factor);
    const uint16_t slot = API.fontSize(size, factor);
    uint16_t count = 0U;

    for (const char *scan = text; *scan != '\0' && count < max; scan++)
    {
        const uint8_t ascii = (uint8_t)*scan;
        if (ascii >= 0x20U && ascii <= 0x7EU && pool_size >= slot)
        {
            API.fontScale(ascii, size, factor, pool);
            out[count++] = (Sprite){.x = x, .y = y, .w = sw, .h = sh, .z = z,
                                    .flags = 0U, .pixels = pool, .palette = palette};
            pool += slot;
            pool_size = (uint16_t)(pool_size - slot);
        }
        x = (int16_t)(x + sw + factor);
    }
    return count;
}
