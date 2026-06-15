#include "renderer.h"
#include "string.h"
#include "ILI9341.h"
#include "sysclock.h"
#include "stdio.h"

/* The renderer is the per-frame hot path, so it is compiled optimised even in
 * debug builds; the rest of the firmware stays at -Og for debuggability. */
#pragma GCC optimize("O3")

#define MAX_SPRITES_BG 350U
#define MAX_SPRITES_FG 350U
#define MAX_SPRITES_UI 350U

#define RENDERER_SCANLINES 16U
#define RENDERER_SCANLINE_BUF_SIZE (RENDERER_SCANLINES * RENDERER_WIDTH)

/* 4-byte aligned so the opaque blitter can write two pixels per 32-bit store. */
static uint16_t s_scanline_buffer[2U][RENDERER_SCANLINE_BUF_SIZE] __attribute__((aligned(4)));

/* The game owns the Sprite storage and submits one contiguous array per layer;
 * the renderer borrows just the base pointer + count. A submitted array must
 * stay valid until rendererRender() returns. */
static const Sprite *s_layer_sprites[LAYER_COUNT];
static uint16_t s_active_sprites[LAYER_COUNT];
static const uint16_t s_max_sprites_per_layer[LAYER_COUNT] = {
    MAX_SPRITES_BG, MAX_SPRITES_FG, MAX_SPRITES_UI};

static const Sprite *s_sorted[LAYER_COUNT][MAX_SPRITES_BG];
static uint16_t s_sorted_count[LAYER_COUNT];

/* Scratch palette copied out of (slow) flash once per sprite. Held at a fixed
 * address so the compositors index it with a single hoisted-base load per pixel
 * instead of recomputing a stack-relative address. Single-threaded renderer, so
 * a shared scratch is safe. */
static uint16_t s_lut[16];

/* Adjacent-pixel pairs of s_lut packed into 32-bit words, indexed by a 4-bit
 * (two 2bpp pixels) source nibble. Lets the opaque blitter emit one 32-bit store
 * per two pixels. Rebuilt from s_lut for each opaque 2bpp sprite. */
static uint32_t s_pair[16];

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

void rendererInit(void)
{
    memset(&s_scanline_buffer[0U], 0U, sizeof(s_scanline_buffer));
    memset(s_active_sprites, 0U, sizeof(s_active_sprites));
}

/* Start a new frame: drop every layer. Only the counts need clearing — a layer
 * with count 0 is skipped by the sort, so its stale base pointer is never read.
 * Call before the frame's rendererSubmitLayer() calls; any layer left
 * unsubmitted simply renders nothing. */
void rendererClear(void)
{
    memset(s_active_sprites, 0U, sizeof(s_active_sprites));
}

/* Set the color painted where no sprite draws (enables per-chunk background
 * fill). RGB565. Until called, uncovered pixels keep stale buffer contents. */
void rendererSetBackground(uint16_t color)
{
    s_bg_color = color;
    s_bg_enabled = true;
}

/* Borrow a layer's whole sprite array (game-owned, kept alive until render). */
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count)
{
    if (layer >= LAYER_COUNT)
    {
        return;
    }
    if (count > s_max_sprites_per_layer[layer])
    {
        count = s_max_sprites_per_layer[layer];
    }
    s_layer_sprites[layer] = sprites;
    s_active_sprites[layer] = count;
}

/* Counting sort: sprites ordered by z ascending within each layer */
static void sortSpritesByZ(void)
{
    for (uint8_t layer = 0U; layer < LAYER_COUNT; layer++)
    {
        uint16_t count = s_active_sprites[layer];
        if (count == 0U)
        {
            s_sorted_count[layer] = 0U;
            continue;
        }

        const Sprite *sprites = s_layer_sprites[layer];
        uint16_t z_count[256];
        memset(z_count, 0, sizeof(z_count));
        for (uint16_t i = 0U; i < count; i++)
        {
            z_count[sprites[i].z]++;
        }

        uint16_t total = 0U;
        for (uint16_t z = 0U; z < 256U; z++)
        {
            uint16_t c = z_count[z];
            z_count[z] = total;
            total += c;
        }
        for (uint16_t i = 0U; i < count; i++)
        {
            s_sorted[layer][z_count[sprites[i].z]++] = &sprites[i];
        }
        s_sorted_count[layer] = count;
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

    /* Vertical clip to the chunk. */
    uint16_t row_top = (sprite_y > (int16_t)start_y) ? (uint16_t)sprite_y : start_y;
    int16_t bottom = sprite_y + (int16_t)sprite_h; /* exclusive */
    uint16_t row_bot = (bottom < (int16_t)end_y) ? (uint16_t)bottom : end_y;
    if (row_bot <= row_top)
    {
        return false;
    }

    /* Horizontal clip (independent of y). */
    int16_t sprite_x = sprite->x;
    uint16_t sprite_w = sprite->w;
    uint16_t x_start = (sprite_x > 0) ? (uint16_t)sprite_x : 0U;
    int16_t right = sprite_x + (int16_t)sprite_w;
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

/* Composite one 2bpp row span. `opaque` is passed as a compile-time constant
 * from the two call sites so the compiler emits a branch-free fast path (no
 * per-pixel transparency test) for sprites flagged SPRITE_OPAQUE, and the
 * index==0 skip path for the rest. Palette lookups hit the fixed-address s_lut
 * (one hoisted-base load per pixel). */
__attribute__((always_inline)) static inline void blitRow2bpp(uint16_t *d, const uint8_t *row, uint16_t col,
                                                              uint16_t n, bool opaque)
{
    /* Head: advance to a 4-pixel (1 byte) boundary. */
    while ((col & 3U) && n)
    {
        uint8_t idx = (row[col >> 2] >> (6U - ((col & 3U) << 1U))) & 3U;
        if (opaque || idx)
        {
            *d = s_lut[idx];
        }
        d++;
        col++;
        n--;
    }
    /* Body: one source byte = four pixels, constant shifts. */
    const uint8_t *p = &row[col >> 2];
    if (opaque)
    {
        /* When the destination is word-aligned, emit two pixels per 32-bit store
         * via the pair-LUT; the head loop above has already restored byte/word
         * alignment for sprites whose left edge is unaligned. */
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
    }
    else
    {
        while (n >= 4U)
        {
            uint8_t b = *p++;
            if (b) /* skip four fully-transparent pixels at once */
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
    /* Tail: at most three trailing pixels. */
    while (n)
    {
        uint8_t idx = (row[col >> 2] >> (6U - ((col & 3U) << 1U))) & 3U;
        if (opaque || idx)
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

/* 2bpp/4bpp compositors. Each copies its palette into the fixed s_lut once, then
 * walks the sprite's rows, advancing the source/destination pointers by a stride
 * per scanline (no per-row multiply). The non-flipped opaque/transparent split is
 * hoisted out of the row loop so each variant is a tight, specialised blit. */
__attribute__((noinline, section(".RamFunc"))) static void composite2bpp(uint16_t *chunk_buffer, uint16_t start_y,
                                                                         uint16_t count, const Sprite *sprite)
{
    SpriteClip clip;
    if (!clipSprite(sprite, start_y, count, &clip))
    {
        return;
    }

    const uint16_t *pal = sprite->palette;
    bool flip_h = (sprite->flags & SPRITE_FLIP_H) != 0U;
    bool opaque = (sprite->flags & SPRITE_OPAQUE) != 0U;

    if (pal != s_lut_pal) /* reload the 4-entry palette only when it changed */
    {
        s_lut[0] = pal[0];
        s_lut[1] = pal[1];
        s_lut[2] = pal[2];
        s_lut[3] = pal[3];
        s_lut_pal = pal;
        s_pair_pal = NULL; /* s_lut changed -> the paired table is now stale */
    }
    if (opaque && pal != s_pair_pal) /* pack adjacent-pixel pairs for the 32-bit store */
    {
        for (uint8_t k = 0U; k < 16U; k++)
        {
            s_pair[k] = (uint32_t)s_lut[k >> 2] | ((uint32_t)s_lut[k & 3U] << 16);
        }
        s_pair_pal = pal;
    }

    uint16_t stride = (sprite->w + 3U) >> 2U;
    int row_step = (sprite->flags & SPRITE_FLIP_V) ? -(int)stride : (int)stride;
    const uint8_t *row = &sprite->pixels[clip.src_top * stride];
    uint16_t *dst = &chunk_buffer[(uint16_t)(clip.row_top - start_y) * RENDERER_WIDTH + clip.x_start];
    uint16_t rows = clip.row_bot - clip.row_top;

    for (uint16_t r = 0U; r < rows; r++)
    {
        if (flip_h)
        {
            blitRowFlip(dst, row, sprite->w, clip.col_left, clip.span, false);
        }
        else if (opaque)
        {
            blitRow2bpp(dst, row, clip.col_left, clip.span, true);
        }
        else
        {
            blitRow2bpp(dst, row, clip.col_left, clip.span, false);
        }
        row += row_step;
        dst += RENDERER_WIDTH;
    }
}

__attribute__((noinline, section(".RamFunc"))) static void composite4bpp(uint16_t *chunk_buffer, uint16_t start_y,
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

/* Render one chunk: walk the z-sorted lists back-to-front (BG low-z → BG high-z
 * → FG → UI) and composite the sprites that overlap this strip. A cheap vertical
 * reject skips the rest, so there is no per-chunk sprite list to build or bound —
 * sprites of any height are handled, and the renderer keeps O(1) extra memory
 * instead of a pool that would have to be sized for the tallest possible sprite.
 *
 * Each sprite fills all of its own rows in the chunk before the next (higher)
 * one paints over it, which keeps the painter's order identical to a
 * scanline-major walk while amortising the per-sprite setup across its rows. */
__attribute__((noinline, section(".RamFunc"))) static void renderScanlineChunk(uint16_t *chunk_buffer, uint16_t start_y, uint16_t count)
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

    int chunk_top = (int)start_y;
    int chunk_bottom = (int)start_y + (int)count; /* exclusive */

    for (uint8_t layer = 0U; layer < LAYER_COUNT; layer++)
    {
        const Sprite *const *sorted = s_sorted[layer];
        uint16_t sprite_count = s_sorted_count[layer];
        for (uint16_t i = 0U; i < sprite_count; i++)
        {
            const Sprite *sprite = sorted[i];
            int top = (int)sprite->y;
            if (top >= chunk_bottom || top + (int)sprite->h <= chunk_top)
            {
                continue; /* sprite does not touch this strip */
            }
            if (sprite->flags & SPRITE_IS_FMT_4BPP)
            {
                composite4bpp(chunk_buffer, start_y, count, sprite);
            }
            else
            {
                composite2bpp(chunk_buffer, start_y, count, sprite);
            }
        }
    }
}

void rendererRender(void)
{
    uint16_t scanline_y = 0U;
    sortSpritesByZ();
    s_lut_pal = NULL; /* invalidate the palette cache (palettes may change per frame) */
    s_pair_pal = NULL;

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
}

uint16_t rendererGetWidthPixels(void)
{
    return RENDERER_WIDTH;
}
uint16_t rendererGetHeightPixels(void)
{
    return RENDERER_HEIGHT;
}
