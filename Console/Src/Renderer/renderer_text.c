#include "Renderer/renderer.h"
#include "Renderer/renderer_internal.h"
#include "Fonts/font_utils.h" /* fontGlyphW/H, fontGet, fontScale, fontSize */
#include <stddef.h>     /* NULL */
#include <stdbool.h>

/*
 * Console-side text drawing — the cold path behind rendererDrawText. Split out of
 * the -O3 renderer TU so its glyph expansion, scaled-glyph cache, and the sole
 * dependency on font_utils don't sit in the per-frame compositor's translation unit.
 * It builds one Sprite per glyph into the shared text state (renderer_internal.h);
 * the renderer core's z-sort then folds those glyphs into their layer's run.
 */

/* Scaled-glyph cache: a glyph at scale > 1 is unpacked/re-packed by fontScale into
 * a slot here once and reused across frames (the expensive part of scaled text).
 * Keyed by (char, font, scale); the slot is sized for the largest supported glyph
 * (FONT_8x8 at RENDERER_TEXT_MAX_SCALE = 32x32 2bpp = 256 B). A slot touched in the
 * current frame is never evicted, so pointers handed out this frame stay valid until
 * rendererRender composites them. Scale 1 is not cached — fontGet returns the glyph
 * straight out of flash. */
#define GLYPH_CACHE_SLOTS 10U
#define GLYPH_CACHE_SLOT_BYTES 256U
typedef struct
{
    uint8_t bits[GLYPH_CACHE_SLOT_BYTES];
    uint32_t used_frame; /* frame counter when last drawn (eviction guard + LRU) */
    uint8_t ch;
    uint8_t font;
    uint8_t scale;
    bool valid;
} GlyphCacheSlot;
static GlyphCacheSlot s_glyph_cache[GLYPH_CACHE_SLOTS];

/* Find (or add) the single-tint palette {transparent, color, color, color} for a
 * drawText call, de-duplicated by colour within the frame. If the small pool is
 * full (many distinct text colours in one frame), reuse the last entry. */
static const uint16_t *textPalette(uint16_t color)
{
    for (uint16_t i = 0U; i < s_text_palette_count; i++)
    {
        if (s_text_palettes[i][1] == color)
        {
            return s_text_palettes[i];
        }
    }
    uint16_t slot = (s_text_palette_count < TEXT_PALETTE_CAP) ? s_text_palette_count++ : (TEXT_PALETTE_CAP - 1U);
    s_text_palettes[slot][0] = 0U; /* index 0 is transparent */
    s_text_palettes[slot][1] = color;
    s_text_palettes[slot][2] = color;
    s_text_palettes[slot][3] = color;
    return s_text_palettes[slot];
}

/* Return a scaled glyph's 2bpp bitmap, unpacking it into a cache slot on first use
 * and reusing it thereafter. NULL only if every slot is already in use this frame
 * (pathological: > GLYPH_CACHE_SLOTS distinct scaled glyphs in one frame) — the
 * caller then skips that glyph. Only called for scale > 1. */
static const uint8_t *glyphCacheGet(uint8_t ch, FontSize font, uint8_t scale)
{
    for (uint8_t i = 0U; i < GLYPH_CACHE_SLOTS; i++)
    {
        GlyphCacheSlot *s = &s_glyph_cache[i];
        if (s->valid && s->ch == ch && s->font == (uint8_t)font && s->scale == scale)
        {
            s->used_frame = s_frame_counter;
            return s->bits;
        }
    }
    /* Miss: take a free slot, else the least-recently-used slot not touched this
     * frame (evicting an in-use slot would dangle a pointer already handed out). */
    GlyphCacheSlot *victim = NULL;
    for (uint8_t i = 0U; i < GLYPH_CACHE_SLOTS; i++)
    {
        if (!s_glyph_cache[i].valid)
        {
            victim = &s_glyph_cache[i];
            break;
        }
    }
    if (victim == NULL)
    {
        uint32_t oldest = 0xFFFFFFFFU;
        for (uint8_t i = 0U; i < GLYPH_CACHE_SLOTS; i++)
        {
            GlyphCacheSlot *s = &s_glyph_cache[i];
            if (s->used_frame != s_frame_counter && s->used_frame <= oldest)
            {
                oldest = s->used_frame;
                victim = s;
            }
        }
    }
    if (victim == NULL || fontSize(font, scale) > GLYPH_CACHE_SLOT_BYTES)
    {
        return NULL;
    }
    fontScale(ch, font, scale, victim->bits);
    victim->ch = ch;
    victim->font = (uint8_t)font;
    victim->scale = scale;
    victim->valid = true;
    victim->used_frame = s_frame_counter;
    return victim->bits;
}

void rendererDrawText(Layer layer, int16_t x, int16_t y, uint8_t z, FontSize font,
                      uint8_t scale, uint16_t color, const char *text)
{
    if (layer >= LAYER_COUNT || text == NULL || (unsigned)font > (unsigned)FONT_8x8)
    {
        return; /* font bound guards the fontGlyphW/fontGet table lookups */
    }
    if (scale == 0U)
    {
        scale = 1U;
    }
    if (scale > RENDERER_TEXT_MAX_SCALE)
    {
        scale = RENDERER_TEXT_MAX_SCALE;
    }
    const uint16_t glyph_w = (uint16_t)(fontGlyphW(font) * scale);
    const uint16_t glyph_h = (uint16_t)(fontGlyphH(font) * scale);
    const int16_t advance = (int16_t)((fontGlyphW(font) + 1U) * scale);
    const uint16_t *pal = textPalette(color);

    for (const char *s = text; *s != '\0'; s++, x = (int16_t)(x + advance))
    {
        const uint8_t ch = (uint8_t)*s;
        if (ch <= 0x20U || ch > 0x7EU) /* skip space (and non-printables): draws nothing */
        {
            continue;
        }
        if (s_text_count >= TEXT_SPRITE_CAP)
        {
            break; /* pool full: drop the rest (bounded) */
        }
        const uint8_t *pixels;
        if (scale == 1U)
        {
            fontGet(ch, font, &pixels); /* straight from flash — no cache needed */
        }
        else if ((pixels = glyphCacheGet(ch, font, scale)) == NULL)
        {
            continue; /* cache exhausted this frame */
        }
        s_text_layer[s_text_count] = (uint8_t)layer;
        s_text_sprites[s_text_count] = (Sprite){.x = x, .y = y, .w = glyph_w, .h = glyph_h,
                                                .z = z, .flags = 0U, .pixels = pixels, .palette = pal};
        s_text_count++;
    }
}
