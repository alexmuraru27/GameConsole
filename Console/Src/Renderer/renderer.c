#include "renderer.h"
#include "string.h"
#include "ILI9341.h"
#include "sysclock.h"
#include "stdio.h"
#include "logger.h"
#include "font_utils.h" /* fontGlyphW/H, fontGet, fontScale, fontSize (rendererDrawText) */

/* The renderer is the per-frame hot path, so it is compiled optimised even in
 * debug builds; the rest of the firmware stays at -Og for debuggability. */
#pragma GCC optimize("O3")

/* Placement of the hot compositor functions: (I-cached) FLASH, not .RamFunc.
 * Measured +40% on TestRenderer (Min/Avg/Max 41/65/125 -> 58/92/166) once the
 * instruction cache was enabled — running the code from flash keeps the per-pixel
 * stores from contending with instruction fetch on the SRAM bus (in .RamFunc the
 * two share it). This reverses the earlier prefetch-only measurement that put the
 * compositors in .RamFunc (docu/renderer.md §7.7/§8). Kept as a named attribute
 * so placement is a single switch if a future (e.g. very text-heavy) workload
 * ever wants the hot code back in SRAM — e.g. (noinline, section(".RamFunc")). */
#define RENDERER_HOT __attribute__((noinline))

/* One sprite budget shared by the three layers: a frame may split it across
 * BG/FG/UI any way it likes (a single layer can spend all of it) — only the
 * frame's total is capped. Replaces the old fixed 350-per-layer split at the
 * same RAM cost (the z-sort pool below is 4 bytes per slot either way). */
#define MAX_SPRITES_TOTAL 1050U

/* Console-side text (rendererDrawText): the renderer expands a string into one
 * glyph Sprite per printable character, drawn from its own pool rather than a
 * game-managed array, so a game submits text with a single syscall and keeps no
 * glyph storage of its own. These glyph sprites join the per-layer z-sort below
 * alongside the game's submitted sprites. TEXT_SPRITE_CAP bounds the on-screen
 * glyph count per frame (across all layers). */
#define TEXT_SPRITE_CAP 256U

#define RENDERER_SCANLINES 16U
#define RENDERER_SCANLINE_BUF_SIZE (RENDERER_SCANLINES * RENDERER_WIDTH)

/* 4-byte aligned so the opaque blitter can write two pixels per 32-bit store. */
static uint16_t s_scanline_buffer[2U][RENDERER_SCANLINE_BUF_SIZE] __attribute__((aligned(4)));

/* The game owns the Sprite storage and submits one contiguous array per layer;
 * the renderer borrows just the base pointer + count. A submitted array must
 * stay valid until rendererRender() returns. */
static const Sprite *s_layer_sprites[LAYER_COUNT];
static uint16_t s_active_sprites[LAYER_COUNT];

/* Flat z-sort pool, shared by the three layers. sortSpritesByZ() lays each
 * layer's z-ascending run back-to-back in layer order, so afterwards
 * s_sorted[0..s_sorted_total) reads as the frame's complete painter order
 * (BG low→high z, then FG, then UI) and binning walks it linearly. Submission
 * keeps the summed layer counts within MAX_SPRITES_TOTAL, so the runs always
 * fit. */
static const Sprite *s_sorted[MAX_SPRITES_TOTAL + TEXT_SPRITE_CAP];
static uint16_t s_sorted_total;

/* Per-chunk sprite bins, rebuilt each frame from the z-sorted pool. This replaces
 * the old per-chunk reject scan: every sprite is appended once to each chunk it
 * spans (in painter order — BG, then FG, then UI, z-ascending within each), so a
 * chunk composites only the sprites that actually touch it instead of testing all of
 * them. Capped per chunk to bound RAM; a chunk that would exceed the cap drops the
 * extra sprites (the cap sits far above any realistic per-chunk count). Unlike the
 * flat pool removed earlier, this is sized by sprite *count*, not sprite *height* — a
 * tall sprite simply appears in more chunks' bins, never overflowing a height-based
 * bound. Entries are z-sort-pool indices, not pointers: half the RAM of a Sprite*
 * bin for one extra s_sorted[] load per sprite per chunk in renderScanlineChunk().
 * 15 chunks * 200 * 2 B = 6000 B. */
#define RENDERER_MAX_CHUNKS ((RENDERER_HEIGHT + RENDERER_SCANLINES - 1U) / RENDERER_SCANLINES)
#define CHUNK_BIN_CAP 200U

static uint16_t s_chunk_bin[RENDERER_MAX_CHUNKS][CHUNK_BIN_CAP];
static uint16_t s_chunk_bin_count[RENDERER_MAX_CHUNKS];

/* Scratch palette copied out of (slow) flash once per sprite. Held at a fixed
 * address so the compositors index it with a single hoisted-base load per pixel
 * instead of recomputing a stack-relative address. Single-threaded renderer, so
 * a shared scratch is safe. */
static uint16_t s_lut[16];

/* Adjacent-pixel pairs of s_lut packed into 32-bit words, indexed by a 4-bit
 * (two 2bpp pixels) source nibble. Lets the blitter emit one 32-bit store per two
 * pixels. Rebuilt from s_lut whenever a 2bpp sprite's palette changes (used by the
 * opaque path and by the transparent path's fully-covered bytes). */
static uint32_t s_pair[16];

/* "Has no transparent (index-0) pixel" lookup for 2bpp source bytes (4 px/byte):
 * a non-zero entry means every pixel the byte encodes is opaque, so the transparent
 * compositor can take the unconditional two-store pair path for that byte and only
 * branch per-pixel on the partially-transparent edge bytes. Built once in
 * rendererInit(); read-only in the hot path. (4bpp gains nothing from this — it has
 * no two-for-one store — so only the 2bpp table exists.) */
static uint8_t s_byte_full_2bpp[256];

/* Palette pointer currently loaded into s_lut / s_pair. Consecutive sprites that
 * share a palette (the common case — a layer's tiles all use one) then skip the
 * reload and the pair rebuild. Reset each frame in case a palette was animated. */
static const uint16_t *s_lut_pal;
static const uint16_t *s_pair_pal;

/* Optional background: when enabled, each chunk is filled with this color before
 * compositing, so pixels no sprite covers show the background instead of stale
 * buffer contents. Disabled by default (a scene that fully covers the screen
 * pays nothing). */
static uint16_t s_bg_color;
static bool s_bg_enabled;

/* Decoded-tile cache: an opaque, unflipped, small tile is unpacked to ready-made
 * RGB565 once per frame (keyed by its pixels+palette+size), then every instance is
 * composited with a plain row copy instead of re-running the 2bpp/4bpp unpack +
 * palette lookup. Pays off when a few tiles are drawn many times (the tile-game
 * norm). Filled lazily; reset each frame (a palette may change under the same
 * pointer between frames). Tiles larger than the cap, flipped, or transparent skip
 * the cache and take the normal compositor. */
#define TILE_CACHE_SLOTS 8U
#define TILE_CACHE_MAX_PX 256U /* up to 16x16 */
typedef struct
{
    const uint8_t *pixels;
    const uint16_t *palette;
    uint16_t w;
    uint16_t h;
    uint16_t rgb[TILE_CACHE_MAX_PX];
} TileCacheSlot;
static TileCacheSlot s_tile_cache[TILE_CACHE_SLOTS];
static uint8_t s_tile_cache_count;

/* ---- Console-side text drawing (rendererDrawText) ----
 * A frame's text glyphs live here, one Sprite each, tagged with their layer; the
 * z-sort folds them into the matching layer's run. Rebuilt every frame (reset in
 * rendererClear/rendererRender), so a game re-issues its text like its sprites. */
static Sprite s_text_sprites[TEXT_SPRITE_CAP];
static uint8_t s_text_layer[TEXT_SPRITE_CAP]; /* Layer of each glyph in s_text_sprites */
static uint16_t s_text_count;

/* Text is single-tint, so each drawText call needs a {transparent, ink, ink, ink}
 * palette. They are pooled and de-duplicated by colour within a frame (a HUD uses
 * only a few colours), and each must outlive compositing — hence console-owned. */
#define TEXT_PALETTE_CAP 16U
static uint16_t s_text_palettes[TEXT_PALETTE_CAP][4];
static uint16_t s_text_palette_count;

/* Scaled-glyph cache: a glyph at scale > 1 is unpacked/re-packed by fontScale into
 * a slot here once and reused across frames (the expensive part of scaled text).
 * Keyed by (char, font, scale); the slot is sized for the largest supported glyph
 * (FONT_8x8 at RENDERER_TEXT_MAX_SCALE = 32x32 2bpp = 256 B). A slot touched in the
 * current frame is never evicted, so pointers handed out this frame stay valid
 * until rendererRender composites them. Scale 1 is not cached — fontGet returns the
 * glyph straight out of flash. */
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
static uint32_t s_frame_counter; /* bumped once per rendererRender */

void rendererInit(void)
{
    memset(&s_scanline_buffer[0U], 0U, sizeof(s_scanline_buffer));
    memset(s_active_sprites, 0U, sizeof(s_active_sprites));

    /* A 2bpp byte is "full" (no transparent pixel) when none of its four 2-bit
     * fields is index 0. */
    for (uint16_t b = 0U; b < 256U; b++)
    {
        bool full2 = (b & 0xC0U) && (b & 0x30U) && (b & 0x0CU) && (b & 0x03U);
        s_byte_full_2bpp[b] = full2 ? 1U : 0U;
    }
    /* Init only — the per-frame render path is never logged (hot loop). */
    LOGGER_LOG_INFO(LOGGER_RENDERER, "init: %ux%u RGB565, 3 layers, %u shared sprite budget", (unsigned)rendererGetWidthPixels(), (unsigned)rendererGetHeightPixels(), (unsigned)MAX_SPRITES_TOTAL);
}

/* Start a new frame: drop every layer. Only the counts need clearing — a layer
 * with count 0 is skipped by the sort, so its stale base pointer is never read.
 * Call before the frame's rendererSubmitLayer() calls; any layer left
 * unsubmitted simply renders nothing. */
void rendererClear(void)
{
    memset(s_active_sprites, 0U, sizeof(s_active_sprites));
    s_text_count = 0U;
    s_text_palette_count = 0U;
}

/* Reset the per-game renderer state at launch (console-side, not a game syscall):
 * drop every layer and disable the background fill, so a freshly loaded game
 * starts from a clean slate and never inherits the menu's background color if it
 * forgets to set its own. The one-time compositor-table / scanline-buffer setup
 * stays in rendererInit() (boot); this only clears the mutable frame state. */
void rendererResetState(void)
{
    memset(s_active_sprites, 0U, sizeof(s_active_sprites));
    s_text_count = 0U;
    s_text_palette_count = 0U;
    s_bg_enabled = false;
    /* The glyph cache is font data (game-independent), so it is kept warm across
     * game launches; only the per-frame text state is cleared here. */
}

/* Set the color painted where no sprite draws (enables per-chunk background
 * fill). RGB565. Until called, uncovered pixels keep stale buffer contents. */
void rendererSetBackground(uint16_t color)
{
    rendererSetBackgroundState(true, color);
}

/* Console-only: read/restore the mutable background-fill state (color + whether
 * enabled). A full-screen OS modal over a running game (the osTextInput keyboard)
 * saves the game's state, paints its own backdrop, then restores it verbatim on
 * close — games set their background once at init, so the modal must not clobber
 * it. Not a game syscall. */
void rendererGetBackground(bool *enabled, uint16_t *color)
{
    *enabled = s_bg_enabled;
    *color = s_bg_color;
}

void rendererSetBackgroundState(bool enabled, uint16_t color)
{
    s_bg_color = color;
    s_bg_enabled = enabled;
}

/* Borrow a layer's whole sprite array (game-owned, kept alive until render).
 * The count is clamped to what the shared budget has left after the other
 * layers' submissions — resubmitting a layer releases its previous share first,
 * so the summed counts never exceed MAX_SPRITES_TOTAL (the sort pool's size). */
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count)
{
    if (layer >= LAYER_COUNT)
    {
        return;
    }
    uint16_t other_layers = 0U;
    for (uint8_t l = 0U; l < LAYER_COUNT; l++)
    {
        if (l != (uint8_t)layer)
        {
            other_layers += s_active_sprites[l];
        }
    }
    uint16_t avail = (uint16_t)(MAX_SPRITES_TOTAL - other_layers);
    if (count > avail)
    {
        count = avail;
    }
    s_layer_sprites[layer] = sprites;
    s_active_sprites[layer] = count;
}

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

/* Counting sort each layer by z ascending, scattering straight into the flat
 * pool with each layer's run starting where the previous layer's ended. After
 * all three, s_sorted[0..s_sorted_total) is the frame's painter order. */
static void sortSpritesByZ(void)
{
    uint16_t total = 0U;
    for (uint8_t layer = 0U; layer < LAYER_COUNT; layer++)
    {
        uint16_t count = s_active_sprites[layer];
        /* Skip an empty layer only when no console text exists at all; otherwise the
         * layer may still carry drawText glyphs, so it must be processed. */
        if (count == 0U && s_text_count == 0U)
        {
            continue;
        }

        const Sprite *sprites = s_layer_sprites[layer];
        uint16_t z_count[256];
        memset(z_count, 0, sizeof(z_count));
        for (uint16_t i = 0U; i < count; i++)
        {
            z_count[sprites[i].z]++;
        }
        /* Console-drawn text glyphs tagged to this layer share its z-run. */
        for (uint16_t i = 0U; i < s_text_count; i++)
        {
            if (s_text_layer[i] == layer)
            {
                z_count[s_text_sprites[i].z]++;
            }
        }

        /* Exclusive prefix sum, based at this layer's run start in the pool. */
        uint16_t offset = total;
        for (uint16_t z = 0U; z < 256U; z++)
        {
            uint16_t c = z_count[z];
            z_count[z] = offset;
            offset += c;
        }
        for (uint16_t i = 0U; i < count; i++)
        {
            s_sorted[z_count[sprites[i].z]++] = &sprites[i];
        }
        /* Text after the game sprites: same-z glyphs land on top of game sprites. */
        for (uint16_t i = 0U; i < s_text_count; i++)
        {
            if (s_text_layer[i] == layer)
            {
                s_sorted[z_count[s_text_sprites[i].z]++] = &s_text_sprites[i];
            }
        }
        total = offset;
    }
    s_sorted_total = total;
}

/* Distribute the z-sorted sprites into per-chunk bins. The sort left the pool
 * in the exact order a chunk must draw — lower layers first, lower z first — so
 * one linear walk appends every sprite to the chunks it spans in draw order.
 * A chunk then iterates only its bin — no vertical reject, no walking sprites
 * that miss the strip. */
static void binSpritesIntoChunks(void)
{
    uint16_t num_chunks = RENDERER_MAX_CHUNKS;
    for (uint16_t c = 0U; c < num_chunks; c++)
    {
        s_chunk_bin_count[c] = 0U;
    }

    uint16_t sprite_count = s_sorted_total;
    for (uint16_t i = 0U; i < sprite_count; i++)
    {
        const Sprite *sprite = s_sorted[i];
        int top = (int)sprite->y;
        int bottom = top + (int)sprite->h; /* exclusive */
        if (bottom <= 0 || top >= (int)RENDERER_HEIGHT)
        {
            continue; /* sprite is entirely above or below the screen */
        }
        if (top < 0)
        {
            top = 0;
        }
        uint16_t first = (uint16_t)top / RENDERER_SCANLINES;
        uint16_t last = (uint16_t)(bottom - 1) / RENDERER_SCANLINES;
        if (last >= num_chunks)
        {
            last = num_chunks - 1U;
        }
        for (uint16_t c = first; c <= last; c++)
        {
            uint16_t n = s_chunk_bin_count[c];
            if (n < CHUNK_BIN_CAP) /* drop beyond the cap (far above realistic) */
            {
                s_chunk_bin[c][n] = i; /* the sprite's z-sort-pool index */
                s_chunk_bin_count[c] = n + 1U;
            }
        }
    }
}

/* Clipped placement of a sprite inside a chunk: the screen rows it touches and
 * the matching source coordinates. Computed once per sprite, not per scanline. */
typedef struct
{
    uint16_t row_top;  /* first screen row touched (inclusive)           */
    uint16_t row_bot;  /* one past the last screen row touched           */
    uint16_t x_start;  /* first screen column (inclusive)                */
    uint16_t span;     /* visible width in pixels                        */
    uint16_t col_left; /* leftmost visible source column                 */
    uint16_t src_top;  /* source row aligned to row_top (V-flip applied) */
} SpriteClip;

__attribute__((always_inline)) static inline bool clipSprite(const Sprite *sprite, uint16_t start_y,
                                                             uint16_t count, SpriteClip *clip)
{
    int16_t sprite_y = sprite->y;
    uint16_t sprite_h = sprite->h;
    uint16_t end_y = start_y + count;

    /* Vertical clip to the chunk. A sprite entirely above the screen has a
     * non-positive bottom; bail before the cast below would wrap it to a huge
     * uint16 (binning already culls these, but keep clip self-contained). */
    int16_t bottom = sprite_y + (int16_t)sprite_h; /* exclusive */
    if (bottom <= 0)
    {
        return false;
    }
    uint16_t row_top = (sprite_y > (int16_t)start_y) ? (uint16_t)sprite_y : start_y;
    uint16_t row_bot = (bottom < (int16_t)end_y) ? (uint16_t)bottom : end_y;
    if (row_bot <= row_top)
    {
        return false;
    }

    /* Horizontal clip (independent of y). A sprite entirely off the LEFT edge has
     * a non-positive right edge — bail before (uint16_t)right wraps it huge and
     * makes `span` a ~65k out-of-bounds blit. (Off the right is handled by the
     * RENDERER_WIDTH clamp; binning does not cull horizontally, so this must.) */
    int16_t sprite_x = sprite->x;
    uint16_t sprite_w = sprite->w;
    int16_t right = sprite_x + (int16_t)sprite_w;
    if (right <= 0)
    {
        return false;
    }
    uint16_t x_start = (sprite_x > 0) ? (uint16_t)sprite_x : 0U;
    uint16_t x_end = (right < (int16_t)RENDERER_WIDTH) ? (uint16_t)right : RENDERER_WIDTH;
    if (x_end <= x_start)
    {
        return false;
    }

    uint16_t top = (uint16_t)(row_top - (uint16_t)sprite_y);
    clip->row_top = row_top;
    clip->row_bot = row_bot;
    clip->x_start = x_start;
    clip->span = x_end - x_start;
    clip->col_left = x_start - (uint16_t)sprite_x;
    clip->src_top = (sprite->flags & SPRITE_FLIP_V) ? (sprite_h - 1U - top) : top;
    return true;
}

/* Composite one opaque 2bpp row span — no per-pixel transparency test. A
 * word-aligned destination emits two pixels per 32-bit store via the pair-LUT; the
 * head loop first restores alignment for sprites whose left edge is unaligned.
 * always_inline and deliberately lean: this is the most-common path (opaque tiles),
 * so it is kept as a tight standalone loop and is *not* sharing a function body with
 * the bulkier transparent path. */
__attribute__((always_inline)) static inline void blitRow2bppOpaque(uint16_t *d, const uint8_t *row,
                                                                    uint16_t col, uint16_t n)
{
    while ((col & 3U) && n) /* head: advance to a 4-pixel (1 byte) boundary */
    {
        uint8_t idx = (row[col >> 2] >> (6U - ((col & 3U) << 1U))) & 3U;
        *d = s_lut[idx];
        d++;
        col++;
        n--;
    }
    const uint8_t *p = &row[col >> 2];
    if (((uint32_t)(uintptr_t)d & 3U) == 0U)
    {
        uint32_t *q = (uint32_t *)(void *)d;
        while (n >= 4U)
        {
            uint8_t b = *p++;
            q[0] = s_pair[b >> 4];
            q[1] = s_pair[b & 0x0FU];
            q += 2;
            d += 4;
            col += 4U;
            n -= 4U;
        }
    }
    else
    {
        while (n >= 4U)
        {
            uint8_t b = *p++;
            d[0] = s_lut[(b >> 6) & 3U];
            d[1] = s_lut[(b >> 4) & 3U];
            d[2] = s_lut[(b >> 2) & 3U];
            d[3] = s_lut[b & 3U];
            d += 4;
            col += 4U;
            n -= 4U;
        }
    }
    while (n) /* tail: at most three trailing pixels */
    {
        uint8_t idx = (row[col >> 2] >> (6U - ((col & 3U) << 1U))) & 3U;
        *d = s_lut[idx];
        d++;
        col++;
        n--;
    }
}

/* Composite one transparent 2bpp row span. A fully-covered byte (no index-0 pixel,
 * the common interior case — flagged in s_byte_full_2bpp) takes the same two-store
 * pair path as the opaque blit; only genuinely mixed edge bytes fall through to the
 * per-pixel test, and an all-transparent byte is skipped four pixels at a time. So a
 * transparent sprite's interior runs at opaque speed and only its edges pay for the
 * masking. Inlined into composite2bppTransparent only (never into the opaque
 * compositor), so the two paths never share — and bloat — one function body. */
__attribute__((always_inline)) static inline void blitRow2bppTransparent(uint16_t *d, const uint8_t *row,
                                                                         uint16_t col, uint16_t n)
{
    while ((col & 3U) && n)
    {
        uint8_t idx = (row[col >> 2] >> (6U - ((col & 3U) << 1U))) & 3U;
        if (idx)
        {
            *d = s_lut[idx];
        }
        d++;
        col++;
        n--;
    }
    const uint8_t *p = &row[col >> 2];
    if (((uint32_t)(uintptr_t)d & 3U) == 0U)
    {
        uint32_t *q = (uint32_t *)(void *)d;
        while (n >= 4U)
        {
            uint8_t b = *p++;
            if (s_byte_full_2bpp[b]) /* interior byte: write all four via the pair-LUT */
            {
                q[0] = s_pair[b >> 4];
                q[1] = s_pair[b & 0x0FU];
            }
            else if (b) /* edge byte: composite only the non-zero pixels */
            {
                uint8_t i0 = (b >> 6) & 3U;
                if (i0)
                {
                    d[0] = s_lut[i0];
                }
                uint8_t i1 = (b >> 4) & 3U;
                if (i1)
                {
                    d[1] = s_lut[i1];
                }
                uint8_t i2 = (b >> 2) & 3U;
                if (i2)
                {
                    d[2] = s_lut[i2];
                }
                uint8_t i3 = b & 3U;
                if (i3)
                {
                    d[3] = s_lut[i3];
                }
            }
            q += 2;
            d += 4;
            col += 4U;
            n -= 4U;
        }
    }
    else
    {
        while (n >= 4U)
        {
            uint8_t b = *p++;
            if (s_byte_full_2bpp[b])
            {
                d[0] = s_lut[(b >> 6) & 3U];
                d[1] = s_lut[(b >> 4) & 3U];
                d[2] = s_lut[(b >> 2) & 3U];
                d[3] = s_lut[b & 3U];
            }
            else if (b)
            {
                uint8_t i0 = (b >> 6) & 3U;
                if (i0)
                {
                    d[0] = s_lut[i0];
                }
                uint8_t i1 = (b >> 4) & 3U;
                if (i1)
                {
                    d[1] = s_lut[i1];
                }
                uint8_t i2 = (b >> 2) & 3U;
                if (i2)
                {
                    d[2] = s_lut[i2];
                }
                uint8_t i3 = b & 3U;
                if (i3)
                {
                    d[3] = s_lut[i3];
                }
            }
            d += 4;
            col += 4U;
            n -= 4U;
        }
    }
    while (n)
    {
        uint8_t idx = (row[col >> 2] >> (6U - ((col & 3U) << 1U))) & 3U;
        if (idx)
        {
            *d = s_lut[idx];
        }
        d++;
        col++;
        n--;
    }
}

/* Composite one 4bpp row span (two pixels per source byte). */
__attribute__((always_inline)) static inline void blitRow4bpp(uint16_t *d, const uint8_t *row, uint16_t col,
                                                              uint16_t n, bool opaque)
{
    /* Head: an odd column is the low nibble of its byte. */
    if ((col & 1U) && n)
    {
        uint8_t idx = row[col >> 1] & 0x0FU;
        if (opaque || idx)
        {
            *d = s_lut[idx];
        }
        d++;
        col++;
        n--;
    }
    const uint8_t *p = &row[col >> 1];
    if (opaque)
    {
        /* Two independent 16-bit stores per byte. A 32-bit combine (orr + one store)
         * was measured slower here: the orr serialises the two palette loads, while
         * the separate stores pipeline freely through the M4 store buffer. */
        while (n >= 2U)
        {
            uint8_t b = *p++;
            d[0] = s_lut[b >> 4];
            d[1] = s_lut[b & 0x0FU];
            d += 2;
            n -= 2U;
        }
    }
    else
    {
        while (n >= 2U)
        {
            uint8_t b = *p++;
            uint8_t hi = b >> 4;
            if (hi)
            {
                d[0] = s_lut[hi];
            }
            uint8_t lo = b & 0x0FU;
            if (lo)
            {
                d[1] = s_lut[lo];
            }
            d += 2;
            n -= 2U;
        }
    }
    /* Tail: a single leading-nibble pixel. */
    if (n)
    {
        uint8_t idx = *p >> 4;
        if (opaque || idx)
        {
            *d = s_lut[idx];
        }
    }
}

/* Horizontally-flipped row blit (uncommon path, kept simple and per-pixel). */
__attribute__((always_inline)) static inline void blitRowFlip(uint16_t *d, const uint8_t *row, uint16_t w,
                                                              uint16_t col_left, uint16_t span, bool is_4bpp)
{
    for (uint16_t i = 0U; i < span; i++)
    {
        uint16_t src = (uint16_t)(w - 1U - col_left - i);
        uint8_t idx = is_4bpp ? ((row[src >> 1] >> (4U - ((src & 1U) << 2U))) & 0x0FU)
                              : ((row[src >> 2] >> (6U - ((src & 3U) << 1U))) & 3U);
        if (idx)
        {
            d[i] = s_lut[idx];
        }
    }
}

/* Rebuild the 2bpp palette scratch: the 4-entry s_lut and the 16-entry s_pair pair
 * table (feeding the 32-bit store used by the opaque path and the transparent path's
 * covered bytes). Kept noinline so its unrolled build does not bloat the two
 * compositors that call it — they stay small and the hot blit loops keep their
 * registers. s_pair_pal doubles as the 2bpp cache key: a 4bpp sprite clears it, so a
 * following 2bpp sprite always rebuilds both tables. */
RENDERER_HOT static void build2bppPalette(const uint16_t *pal)
{
    s_lut[0] = pal[0];
    s_lut[1] = pal[1];
    s_lut[2] = pal[2];
    s_lut[3] = pal[3];
    for (uint8_t k = 0U; k < 16U; k++)
    {
        s_pair[k] = (uint32_t)s_lut[k >> 2] | ((uint32_t)s_lut[k & 3U] << 16);
    }
    s_lut_pal = pal;
    s_pair_pal = pal;
}

/* Refresh the 2bpp scratch only when the palette changed since the last 2bpp sprite
 * (the common case is a layer's tiles sharing one palette). Inlined: the unchanged
 * path is a single pointer compare with no call. */
__attribute__((always_inline)) static inline void load2bppPalette(const uint16_t *pal)
{
    if (pal != s_pair_pal)
    {
        build2bppPalette(pal);
    }
}

/* Opaque and transparent 2bpp compositors are separate functions, each inlining
 * only its own row blit, so the two paths never share — and grow — one .RamFunc. The
 * hot opaque case stays as lean as the original single compositor. Each keeps the row
 * walk in plain scalar locals (no address-taken struct) so they live in registers
 * across the row loop. */
RENDERER_HOT static void composite2bppOpaque(uint16_t *chunk_buffer,
                                             uint16_t start_y, uint16_t count,
                                             const Sprite *sprite)
{
    SpriteClip clip;
    if (!clipSprite(sprite, start_y, count, &clip))
    {
        return;
    }
    load2bppPalette(sprite->palette);

    uint16_t stride = (sprite->w + 3U) >> 2U;
    int row_step = (sprite->flags & SPRITE_FLIP_V) ? -(int)stride : (int)stride;
    const uint8_t *row = &sprite->pixels[clip.src_top * stride];
    uint16_t *dst = &chunk_buffer[(uint16_t)(clip.row_top - start_y) * RENDERER_WIDTH + clip.x_start];
    uint16_t rows = clip.row_bot - clip.row_top;

    if (sprite->flags & SPRITE_FLIP_H)
    {
        for (uint16_t r = 0U; r < rows; r++)
        {
            blitRowFlip(dst, row, sprite->w, clip.col_left, clip.span, false);
            row += row_step;
            dst += RENDERER_WIDTH;
        }
    }
    else
    {
        for (uint16_t r = 0U; r < rows; r++)
        {
            blitRow2bppOpaque(dst, row, clip.col_left, clip.span);
            row += row_step;
            dst += RENDERER_WIDTH;
        }
    }
}

RENDERER_HOT static void composite2bppTransparent(uint16_t *chunk_buffer,
                                                  uint16_t start_y, uint16_t count,
                                                  const Sprite *sprite)
{
    SpriteClip clip;
    if (!clipSprite(sprite, start_y, count, &clip))
    {
        return;
    }
    load2bppPalette(sprite->palette);

    uint16_t stride = (sprite->w + 3U) >> 2U;
    int row_step = (sprite->flags & SPRITE_FLIP_V) ? -(int)stride : (int)stride;
    const uint8_t *row = &sprite->pixels[clip.src_top * stride];
    uint16_t *dst = &chunk_buffer[(uint16_t)(clip.row_top - start_y) * RENDERER_WIDTH + clip.x_start];
    uint16_t rows = clip.row_bot - clip.row_top;

    if (sprite->flags & SPRITE_FLIP_H)
    {
        for (uint16_t r = 0U; r < rows; r++)
        {
            blitRowFlip(dst, row, sprite->w, clip.col_left, clip.span, false);
            row += row_step;
            dst += RENDERER_WIDTH;
        }
    }
    else
    {
        for (uint16_t r = 0U; r < rows; r++)
        {
            blitRow2bppTransparent(dst, row, clip.col_left, clip.span);
            row += row_step;
            dst += RENDERER_WIDTH;
        }
    }
}

RENDERER_HOT static void composite4bpp(uint16_t *chunk_buffer, uint16_t start_y,
                                       uint16_t count, const Sprite *sprite)
{
    SpriteClip clip;
    if (!clipSprite(sprite, start_y, count, &clip))
    {
        return;
    }

    const uint16_t *pal = sprite->palette;
    if (pal != s_lut_pal) /* reload the 16-entry palette only when it changed */
    {
        for (uint8_t k = 0U; k < 16U; k++)
        {
            s_lut[k] = pal[k];
        }
        s_lut_pal = pal;
        s_pair_pal = NULL;
    }

    uint16_t stride = (sprite->w + 1U) >> 1U;
    bool flip_h = (sprite->flags & SPRITE_FLIP_H) != 0U;
    bool opaque = (sprite->flags & SPRITE_OPAQUE) != 0U;
    int row_step = (sprite->flags & SPRITE_FLIP_V) ? -(int)stride : (int)stride;
    const uint8_t *row = &sprite->pixels[clip.src_top * stride];
    uint16_t *dst = &chunk_buffer[(uint16_t)(clip.row_top - start_y) * RENDERER_WIDTH + clip.x_start];
    uint16_t rows = clip.row_bot - clip.row_top;

    for (uint16_t r = 0U; r < rows; r++)
    {
        if (flip_h)
        {
            blitRowFlip(dst, row, sprite->w, clip.col_left, clip.span, true);
        }
        else if (opaque)
        {
            blitRow4bpp(dst, row, clip.col_left, clip.span, true);
        }
        else
        {
            blitRow4bpp(dst, row, clip.col_left, clip.span, false);
        }
        row += row_step;
        dst += RENDERER_WIDTH;
    }
}

/* Unpack a whole tile into a cache slot's RGB565 buffer (once per unique tile per
 * frame). Runs rarely, so it stays in flash rather than spending .RamFunc space. */
static void decodeTile(TileCacheSlot *slot, const uint16_t *pal, const uint8_t *pixels,
                       uint16_t w, uint16_t h, bool is_4bpp)
{
    uint16_t *out = slot->rgb;
    if (is_4bpp)
    {
        uint16_t stride = (w + 1U) >> 1U;
        for (uint16_t y = 0U; y < h; y++)
        {
            const uint8_t *row = &pixels[y * stride];
            for (uint16_t x = 0U; x < w; x++)
            {
                uint8_t idx = (x & 1U) ? (row[x >> 1] & 0x0FU) : (row[x >> 1] >> 4);
                *out++ = pal[idx];
            }
        }
    }
    else
    {
        uint16_t stride = (w + 3U) >> 2U;
        for (uint16_t y = 0U; y < h; y++)
        {
            const uint8_t *row = &pixels[y * stride];
            for (uint16_t x = 0U; x < w; x++)
            {
                uint8_t idx = (row[x >> 2] >> (6U - ((x & 3U) << 1U))) & 3U;
                *out++ = pal[idx];
            }
        }
    }
}

/* Return a sprite's decoded RGB565 buffer, decoding it into a free slot on first
 * use. NULL means "not cacheable" (flipped, larger than the cap, or the cache is
 * full) — the caller then takes the normal compositor. */
RENDERER_HOT static const uint16_t *tileCacheLookup(const Sprite *sprite,
                                                    bool is_4bpp)
{
    if ((uint32_t)sprite->w * sprite->h > TILE_CACHE_MAX_PX)
    {
        return NULL;
    }
    if (sprite->flags & (SPRITE_FLIP_H | SPRITE_FLIP_V))
    {
        return NULL;
    }
    const uint8_t *pixels = sprite->pixels;
    const uint16_t *pal = sprite->palette;
    for (uint8_t i = 0U; i < s_tile_cache_count; i++)
    {
        if (s_tile_cache[i].pixels == pixels && s_tile_cache[i].palette == pal &&
            s_tile_cache[i].w == sprite->w && s_tile_cache[i].h == sprite->h)
        {
            return s_tile_cache[i].rgb;
        }
    }
    if (s_tile_cache_count >= TILE_CACHE_SLOTS)
    {
        return NULL;
    }
    TileCacheSlot *slot = &s_tile_cache[s_tile_cache_count++];
    slot->pixels = pixels;
    slot->palette = pal;
    slot->w = sprite->w;
    slot->h = sprite->h;
    decodeTile(slot, pal, pixels, sprite->w, sprite->h, is_4bpp);
    return slot->rgb;
}

/* Composite a cached (already-RGB565) opaque tile: a plain per-row copy, no unpack
 * or palette lookup. Only reached for unflipped tiles, so the source advances by a
 * positive row stride. */
RENDERER_HOT static void compositeCachedOpaque(uint16_t *chunk_buffer,
                                               uint16_t start_y, uint16_t count,
                                               const Sprite *sprite,
                                               const uint16_t *rgb)
{
    SpriteClip clip;
    if (!clipSprite(sprite, start_y, count, &clip))
    {
        return;
    }
    uint16_t w = sprite->w;
    uint16_t span = clip.span;
    const uint16_t *src = &rgb[(uint32_t)clip.src_top * w + clip.col_left];
    uint16_t *dst = &chunk_buffer[(uint16_t)(clip.row_top - start_y) * RENDERER_WIDTH + clip.x_start];
    uint16_t rows = clip.row_bot - clip.row_top;
    uint16_t words = span >> 1U; /* two pixels per 32-bit word */

    /* Take the word-copy path only when the source alignment is stable across rows:
     * dst advances by an even stride, but an odd tile width shifts src every line, so
     * require an even width (and both ends word-aligned on the first row). The check
     * is hoisted — a per-row test measured ~8% slower on the common even-width tile. */
    if (((((uintptr_t)dst | (uintptr_t)src) & 3U) == 0U) && ((w & 1U) == 0U))
    {
        for (uint16_t r = 0U; r < rows; r++)
        {
            uint32_t *d32 = (uint32_t *)(void *)dst;
            const uint32_t *s32 = (const uint32_t *)(const void *)src;
            for (uint16_t k = 0U; k < words; k++)
            {
                d32[k] = s32[k];
            }
            if (span & 1U)
            {
                dst[span - 1U] = src[span - 1U];
            }
            src += w;
            dst += RENDERER_WIDTH;
        }
    }
    else
    {
        for (uint16_t r = 0U; r < rows; r++)
        {
            for (uint16_t k = 0U; k < span; k++)
            {
                dst[k] = src[k];
            }
            src += w;
            dst += RENDERER_WIDTH;
        }
    }
}

/* Render one chunk: paint the optional background, then composite the chunk's bin
 * (already in back-to-front draw order — BG low-z → BG high-z → FG → UI) into the
 * strip buffer. Opaque tiles that fit the decoded-tile cache blit by row copy; the
 * rest take the unpacking compositors. */
RENDERER_HOT static void renderScanlineChunk(uint16_t *chunk_buffer, uint16_t start_y, uint16_t count)
{
    if (s_bg_enabled) /* paint the background; sprites composite over it */
    {
        uint32_t pair = (uint32_t)s_bg_color | ((uint32_t)s_bg_color << 16);
        uint32_t *fill = (uint32_t *)(void *)chunk_buffer;
        /* 2 px per word; RENDERER_WIDTH/2 (160) is a multiple of 8, so unroll x8. */
        uint32_t blocks = (uint32_t)count * (RENDERER_WIDTH / 16U);
        while (blocks--)
        {
            fill[0] = pair;
            fill[1] = pair;
            fill[2] = pair;
            fill[3] = pair;
            fill[4] = pair;
            fill[5] = pair;
            fill[6] = pair;
            fill[7] = pair;
            fill += 8;
        }
    }

    /* This chunk's sprites are already collected, in draw order, in its bin — so we
     * just composite them. No vertical reject and no scanning sprites that miss the
     * strip (binSpritesIntoChunks did that once per frame). */
    uint16_t chunk_idx = start_y / RENDERER_SCANLINES;
    const uint16_t *bin = s_chunk_bin[chunk_idx];
    uint16_t bin_count = s_chunk_bin_count[chunk_idx];
    for (uint16_t i = 0U; i < bin_count; i++)
    {
        const Sprite *sprite = s_sorted[bin[i]];
        uint8_t flags = sprite->flags;
        if (flags & SPRITE_OPAQUE)
        {
            /* Opaque tiles try the decoded-tile cache first; a miss (too big,
             * flipped, or cache full) falls through to the unpacking compositor. */
            const uint16_t *rgb = tileCacheLookup(sprite, (flags & SPRITE_IS_FMT_4BPP) != 0U);
            if (rgb != NULL)
            {
                compositeCachedOpaque(chunk_buffer, start_y, count, sprite, rgb);
                continue;
            }
        }
        if (flags & SPRITE_IS_FMT_4BPP)
        {
            composite4bpp(chunk_buffer, start_y, count, sprite);
        }
        else if (flags & SPRITE_OPAQUE)
        {
            composite2bppOpaque(chunk_buffer, start_y, count, sprite);
        }
        else
        {
            composite2bppTransparent(chunk_buffer, start_y, count, sprite);
        }
    }
}

void rendererRender(void)
{
    uint16_t scanline_y = 0U;
    sortSpritesByZ();
    binSpritesIntoChunks();
    s_lut_pal = NULL; /* invalidate the palette cache (palettes may change per frame) */
    s_pair_pal = NULL;
    s_tile_cache_count = 0U; /* decoded tiles are only valid within one frame */

    for (uint16_t chunk_index = 0U; scanline_y < RENDERER_HEIGHT; chunk_index++)
    {
        uint16_t lines_remaining = RENDERER_HEIGHT - scanline_y;
        uint16_t chunk_lines = (lines_remaining < RENDERER_SCANLINES) ? lines_remaining : RENDERER_SCANLINES;
        uint16_t buffer_index = chunk_index & 1U;

        /* Composite the next chunk while the previous chunk's DMA is in flight
         * (ili9341DrawScanlines waits on the prior transfer before starting). */
        renderScanlineChunk(s_scanline_buffer[buffer_index], scanline_y, chunk_lines);
        ili9341DrawScanlines((uint8_t)chunk_lines, scanline_y, chunk_lines * RENDERER_WIDTH, s_scanline_buffer[buffer_index]);
        scanline_y += chunk_lines;
    }

    /* Text is re-issued per frame like sprites: drop this frame's glyphs and advance
     * the frame counter (the glyph cache uses it for eviction/LRU). The scaled-glyph
     * bitmaps themselves stay cached across frames. */
    s_text_count = 0U;
    s_text_palette_count = 0U;
    s_frame_counter++;
}

uint16_t rendererGetWidthPixels(void)
{
    return RENDERER_WIDTH;
}
uint16_t rendererGetHeightPixels(void)
{
    return RENDERER_HEIGHT;
}

/* Pixel Forge's 64-color system palette (pixelforge/palette.py) as RGB565.
 * Assets store palette entries as indices into this table; this is where they
 * become panel colors. Generated from the same source as the editor. */
static const uint16_t s_system_palette[64] = {
    0x630C,
    0x0173,
    0x0898,
    0x3818,
    0x6013,
    0x7809,
    0x7800,
    0x60C0,
    0x39A0,
    0x0A60,
    0x02C0,
    0x02C0,
    0x0249,
    0x0000,
    0x0000,
    0x0000,
    0xAD55,
    0x033E,
    0x31FF,
    0x70DF,
    0xA85E,
    0xC871,
    0xC903,
    0xAA20,
    0x7360,
    0x3380,
    0x0500,
    0x04E3,
    0x0451,
    0x0000,
    0x0000,
    0x0000,
    0xFFFF,
    0x4DBF,
    0x847F,
    0xCB5F,
    0xFADF,
    0xFAFC,
    0xFB8D,
    0xFCC0,
    0xCE00,
    0x8700,
    0x4FA0,
    0x2F8D,
    0x2EDC,
    0x4A69,
    0x0000,
    0x0000,
    0xFFFF,
    0xBFFF,
    0xCE9F,
    0xEE3F,
    0xFDFF,
    0xFDFE,
    0xFE38,
    0xFEB3,
    0xEF30,
    0xCF90,
    0xBFD3,
    0xAFD8,
    0xAF9E,
    0xBDD7,
    0x0000,
    0x0000,
};

uint16_t rendererSystemColor(uint8_t system_index)
{
    return s_system_palette[system_index & 0x3FU];
}
