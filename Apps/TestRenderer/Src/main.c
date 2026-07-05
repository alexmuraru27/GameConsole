#include "game_console_api.h"
#include "gfx_asset.h"             /* GfxAssetHeader / GFX1 format (tools/graphics) */
#include "TestRendererAssetEnum.h" /* TESTRENDERER_GFX_* ids (packer-generated) */
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/*
 * TestRenderer — a renderer benchmark, shipped as an ordinary loadable game.
 *
 * It runs the exact path a real game takes (unprivileged, MPU-confined, reaching
 * the renderer only through SVC syscalls, streaming graphics from a .pak), and it
 * is deliberately built to stress the *whole* compositor rather than an ideal one:
 *
 *   - 64 distinct sprites (Assets/gen_tiles.py) overflow the renderer's 8-slot
 *     decoded-tile cache, so most frames hit the uncached 2bpp/4bpp compositors.
 *   - 32 opaque (solid terrain/structures) + 32 transparent (shaped objects/actors)
 *     give a median load across both paths.
 *   - sizes from 8x8 to 64x32: small sprites cache (<=256 px), big ones can't, so
 *     they take the unpacking path and span several scanline chunks; some are drawn
 *     H-flipped to exercise the flipped blit too.
 *
 * It is an endless auto-scroller: a fantasy landscape (sky + mountains + rolling
 * terrain + structures + vegetation + props + actors + a HUD) slides past at a
 * constant real-world speed (getDeltaTimeUs(), so it scrolls uniformly even as the
 * frame rate swings under the load sweep). On top of the base scene the *load is
 * swept 50%->100%->50% of the renderer's budget* (3 layers x 350 = 1050 sprites)
 * by a slow triangle wave that pours a drifting swarm of collectibles in — spread
 * across the screen so no 16-line chunk overflows the compositor's per-chunk bin,
 * and varied so the cache keeps missing. The OS runs games uncapped, so the
 * on-screen counters (current / min / avg / max FPS) show throughput vs load.
 * Special Button 2 quits.
 *
 * Memory: the 64 tile blobs stream into the CCM asset arena (zero-wait for the
 * renderer's CPU reads); three small 4bpp actors go into a GAME_RAM buffer to
 * exercise both pools. The three per-layer Sprite arrays follow in the CCM arena.
 */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define TILE 16
#define COLS (RENDERER_WIDTH / TILE)  /* 20 on-screen columns */
#define ROWS (RENDERER_HEIGHT / TILE) /* 15 rows */
#define VIS_COLS (COLS + 1)           /* one extra column for the partial edge tile */
#define SCROLL_PX_PER_SEC 56.0f       /* world scroll speed (frame-rate independent) */

#define LAYER_CAP 350U
#define SPRITE_BUDGET (3U * LAYER_CAP) /* 1050 */
#define LOAD_MIN_PCT 50U
#define LOAD_MAX_PCT 100U
#define SWEEP_PERIOD_MS 8000U /* one 50 -> 100 -> 50 sweep every 8 s (real time) */

/* ---- Diagnostics (flip to bisect the periodic mid-screen flicker) ----
 * TR_FIXED_LOAD: 0 = normal sweep; else pin the load % (e.g. 75) so the FPS is
 *   CONSTANT. If the periodic flicker vanishes, it's tearing — the uncapped FPS
 *   was sweeping across the panel refresh (no VSync), not a render bug.
 * TR_NO_BIG: 1 = cull every >16x16 sprite (keeps them counted, off-screen) to
 *   rule the big-sprite compositors in or out. */
#define TR_FIXED_LOAD 0
#define TR_NO_BIG 0

/* The procedural world repeats every WORLD_PERIOD_COLS so the camera wraps without
 * unbounded growth; every landmark period below divides it. */
#define WORLD_PERIOD_COLS 120
#define WORLD_PERIOD_PX (WORLD_PERIOD_COLS * TILE)

/* Top-left rectangle holding the FPS/load readouts; filler is kept out of it so the
 * small text stays legible. */
#define READOUT_W 96
#define READOUT_H 60

/* ---- Text ink colours (RGB565). ---- */
#define COL_WHITE 0xFFFFU
#define COL_GREEN 0x07E0U
#define COL_AMBER 0xFD20U
#define COL_CYAN 0x07FFU

/* One loaded sprite: a pointer into its decoded GfxAsset blob, the RGB565 palette
 * (system indices resolved through rendererSystemColor), its size, and the sprite
 * flags (format + opacity) the asset carries. Built once at init. */
typedef struct
{
    const uint8_t *pixels;
    uint16_t palette[16];
    uint16_t w, h;
    uint8_t flags;
} Tile;

#define TILE_COUNT 64
static Tile s_tile[TILE_COUNT];
#define REF(id) (&s_tile[(id) - 1]) /* asset ids are 1-based (TESTRENDERER_GFX_*) */

/* Asset pools: the 64 tile blobs live in the CCM arena; three small 4bpp actors go
 * into GAME_RAM (to exercise both pools); the per-layer Sprite arrays follow the
 * tiles in the CCM arena. */
static uint8_t s_gram_assets[600] __attribute__((aligned(4)));
static Sprite *s_bg, *s_fg, *s_ui; /* [LAYER_CAP] each, in the CCM arena */

/* World/animation state — frame-rate independent (driven by real elapsed time). */
static uint32_t s_anim_ms;
static float s_scroll_px;
static int s_camera_x;

static uint16_t s_load_pct, s_sprite_target;

/* ---- FPS measurement (unchanged: 1 ms tick, 250 ms rolling "current", absolute
 * ratcheting min/max, cumulative avg, a SWO summary once a second). ---- */
#define FPS_WARMUP_FRAMES 5U
static uint16_t s_fps_cur, s_fps_min, s_fps_avg, s_fps_max;
static uint32_t s_last_frame_ms, s_cur_start_ms, s_cur_frames;
static uint32_t s_session_start_ms, s_session_frames, s_log_ms;
static bool s_fps_latched;

/* Build a sprite for tile `t` at (x,y,z), OR-ing in extra flags (e.g. SPRITE_FLIP_H).
 * Size comes from the loaded asset, so variable-size tiles just work. */
static inline Sprite spr(int16_t x, int16_t y, uint8_t z, const Tile *t, uint8_t extra)
{
#if TR_NO_BIG
    if (t->w > 16U || t->h > 16U) /* diagnostic: keep it counted but cull off-screen */
    {
        y = -2000;
    }
#endif
    return (Sprite){.x = x, .y = y, .w = t->w, .h = t->h, .z = z, .flags = (uint8_t)(t->flags | extra), .pixels = t->pixels, .palette = t->palette};
}

/* A 0..amp..0 triangle wave on the real-time clock, for frame-rate-independent bob. */
static int bob(uint32_t phase_ms, uint32_t period_ms, int amp)
{
    uint32_t t = (s_anim_ms + phase_ms) % (period_ms * 2U);
    uint32_t v = (t < period_ms) ? t : (period_ms * 2U - t);
    return (int)((uint32_t)amp * v / period_ms);
}

/* Stream one tile asset from the bound .pak into *cursor (bump-allocated, 4-aligned,
 * bounded by end), decode its header (size + flags + palette), and resolve its
 * system-palette indices to RGB565. */
static bool loadTile(uint32_t id, Tile *out, uint8_t **cursor, const uint8_t *end)
{
    AssetMetaData meta;
    if (assetLoaderGetAssetMetadata(id, &meta) != 0U)
    {
        return false;
    }
    uint8_t *blob = (uint8_t *)(((uintptr_t)*cursor + 3U) & ~(uintptr_t)3U);
    if (blob + meta.size > end || assetLoaderGetAssetData(id, blob, meta.size) != 0U)
    {
        return false;
    }
    *cursor = blob + meta.size;

    const GfxAssetHeader *h = (const GfxAssetHeader *)blob;
    if (h->magic != GFX_ASSET_MAGIC)
    {
        return false;
    }
    const uint8_t *pixels = blob + sizeof(GfxAssetHeader);
    const uint8_t *idx_pal = pixels + h->dataSize;
    const uint32_t colors = (h->format == GFX_FMT_4BPP) ? 16U : 4U;
    for (uint32_t i = 0U; i < colors; i++)
    {
        out->palette[i] = rendererSystemColor(idx_pal[i]);
    }
    out->pixels = pixels;
    out->w = (uint16_t)h->width;
    out->h = (uint16_t)h->height;
    out->flags = (uint8_t)h->flags;
    return true;
}

static void updateLoad(void)
{
#if TR_FIXED_LOAD
    s_load_pct = TR_FIXED_LOAD; /* diagnostic: constant load -> constant FPS */
#else
    const uint32_t phase = getSysTime() % SWEEP_PERIOD_MS, half = SWEEP_PERIOD_MS / 2U;
    const uint32_t tri = (phase < half) ? (phase * 1000U / half)
                                        : (1000U - (phase - half) * 1000U / half);
    s_load_pct = (uint16_t)(LOAD_MIN_PCT + (LOAD_MAX_PCT - LOAD_MIN_PCT) * tri / 1000U);
#endif
    s_sprite_target = (uint16_t)((uint32_t)SPRITE_BUDGET * s_load_pct / 100U);
}

static void fpsSample(void)
{
    const uint32_t now = getSysTime();
    if (s_last_frame_ms == 0U)
    {
        s_last_frame_ms = s_session_start_ms = s_cur_start_ms = s_log_ms = now;
        return;
    }
    uint32_t dt = now - s_last_frame_ms;
    if (dt == 0U)
    {
        dt = 1U;
    }
    const uint16_t inst = (uint16_t)(1000U / dt);
    s_last_frame_ms = now;
    s_session_frames++;
    s_cur_frames++;

    if (s_session_frames > FPS_WARMUP_FRAMES)
    {
        if (!s_fps_latched)
        {
            s_fps_latched = true;
            s_fps_min = s_fps_max = inst;
        }
        else if (inst < s_fps_min)
        {
            s_fps_min = inst;
        }
        else if (inst > s_fps_max)
        {
            s_fps_max = inst;
        }
    }
    const uint32_t elapsed = now - s_session_start_ms;
    if (elapsed > 0U)
    {
        s_fps_avg = (uint16_t)(s_session_frames * 1000U / elapsed);
    }
    if (now - s_cur_start_ms >= 250U)
    {
        s_fps_cur = (uint16_t)(s_cur_frames * 1000U / (now - s_cur_start_ms));
        s_cur_start_ms = now;
        s_cur_frames = 0U;
    }
    if (now - s_log_ms >= 1000U)
    {
        gameLog("renderer test: load=%u%% (%u spr)  fps cur=%u min=%u avg=%u max=%u",
                (unsigned)s_load_pct, (unsigned)s_sprite_target, (unsigned)s_fps_cur,
                (unsigned)s_fps_min, (unsigned)s_fps_avg, (unsigned)s_fps_max);
        s_log_ms = now;
    }
}

/* Rolling-hills ground height (top row of terrain) for an absolute world column. */
static uint8_t terrainTop(uint16_t col)
{
    static const uint8_t profile[8] = {11U, 10U, 9U, 10U, 11U, 10U, 9U, 8U};
    return profile[(col / 2U) % 8U];
}

/* Drifting biome bands keep the terrain from being a monotone field — and span
 * the opaque terrain tiles (grass/sand/snow/path/cobble/water/ice/stone/lava). */
#define NBIOME 9U
static uint8_t biomeOf(uint16_t col) { return (uint8_t)((col / 9U) % NBIOME); }
static const Tile *surfaceTile(uint16_t col)
{
    static const uint32_t biome[NBIOME] = {
        TESTRENDERER_GFX_GRASS, TESTRENDERER_GFX_SAND, TESTRENDERER_GFX_SNOW,
        TESTRENDERER_GFX_PATH, TESTRENDERER_GFX_COBBLE, TESTRENDERER_GFX_WATER,
        TESTRENDERER_GFX_ICE, TESTRENDERER_GFX_STONE, TESTRENDERER_GFX_LAVA};
    return REF(biome[biomeOf(col)]);
}

/* LAYER_BG: the horizon — a soft sky-gradient band up top, distant mountains and
 * hills, and a few drifting clouds. The renderer's background fill paints the open
 * sky behind it, so the BG stays light and leaves budget for the load filler. */
static uint16_t buildBackground(uint16_t first_col)
{
    (void)first_col;
    uint16_t n = 0U;
    /* parallax: distant layers scroll at a fraction of the camera */
    const int far = s_camera_x / 3, mid = s_camera_x / 2;

    /* time of day drifts as you travel: day -> dusk -> starry night */
    const uint32_t tod = (uint32_t)(s_camera_x / 640) % 3U;
    const Tile *sky = (tod == 1U)   ? REF(TESTRENDERER_GFX_SKY_DUSK)
                      : (tod == 2U) ? REF(TESTRENDERER_GFX_STARS)
                                    : REF(TESTRENDERER_GFX_SKY_DAY);

    /* NB: cast RENDERER_WIDTH to int — x starts negative (parallax offset), and an
     * unsigned compare would convert x to a huge value and skip the whole loop. */
    for (int x = -(far % 64); x < (int)RENDERER_WIDTH && n < LAYER_CAP - 8U; x += 64)
    {
        s_bg[n++] = spr((int16_t)x, 96, 0U, REF(TESTRENDERER_GFX_MOUNTAIN), 0U);      /* peaks, base ~y128 */
        s_bg[n++] = spr((int16_t)(x + 32), 112, 1U, REF(TESTRENDERER_GFX_HILLS), 0U); /* near hills in front */
    }
    /* a top gradient band so the sky reads as a gradient, not a flat fill */
    for (uint16_t vc = 0U; vc < VIS_COLS && n < LAYER_CAP - 2U; vc++)
    {
        s_bg[n++] = spr((int16_t)(vc * TILE), 0, 0U, sky, 0U);
    }
    /* an opaque cloud bank plus a few transparent clouds (mid parallax) */
    s_bg[n++] = spr((int16_t)(((40 - mid) % (RENDERER_WIDTH + 96)) - 48), 22, 1U,
                    REF(TESTRENDERER_GFX_CLOUDBANK), 0U);
    for (int k = 0; k < 3 && n < LAYER_CAP - 1U; k++)
    {
        int cx = ((k * 110 + 60 - mid) % (RENDERER_WIDTH + 64)) - 32;
        s_bg[n++] = spr((int16_t)cx, (int16_t)(8 + k * 14), 2U,
                        REF(TESTRENDERER_GFX_CLOUD), (k & 1) ? SPRITE_FLIP_H : 0U);
    }
    return n;
}

/* LAYER_FG: the playable landscape — terrain ground, structures, vegetation, props,
 * items and actors, placed procedurally along the scrolling world. */
static uint16_t buildForeground(uint16_t first_col)
{
    uint16_t n = 0U;
    const Tile *dirt = REF(TESTRENDERER_GFX_DIRT);
    const Tile *dirtd = REF(TESTRENDERER_GFX_DIRT_DARK);

    for (uint16_t vc = 0U; vc < VIS_COLS && n < LAYER_CAP - 32U; vc++)
    {
        uint16_t col = first_col + vc;
        int16_t sx = (int16_t)((int)col * TILE - s_camera_x);
        uint8_t top = terrainTop(col);

        /* ground column: surface tile, then dirt down to the floor */
        s_fg[n++] = spr(sx, (int16_t)(top * TILE), 5U, surfaceTile(col), 0U);
        for (uint8_t r = top + 1U; r < ROWS && n < LAYER_CAP - 12U; r++)
        {
            s_fg[n++] = spr(sx, (int16_t)(r * TILE), 4U, (r & 1U) ? dirt : dirtd, 0U);
        }
        int16_t gy = (int16_t)(top * TILE); /* surface y, for things standing on the ground */

        /* --- scattered vegetation & props (deterministic per world column) --- */
        if (col % 11U == 3U) /* a tall tree (32x48, big -> uncached, spans chunks) */
        {
            s_fg[n++] = spr((int16_t)(sx - 8), (int16_t)(gy - 44), 8U,
                            REF(TESTRENDERER_GFX_TREE), (col & 16U) ? SPRITE_FLIP_H : 0U);
            s_fg[n++] = spr((int16_t)(sx + 4), (int16_t)(gy - 30 + bob((uint32_t)col * 30U, 500U, 3)),
                            14U, REF(TESTRENDERER_GFX_APPLE), 0U); /* an apple in the canopy */
        }
        else if (col % 7U == 2U)
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_BUSH), 0U);
        }
        if (col % 5U == 1U)
        {
            const Tile *deco = (col % 15U == 1U) ? REF(TESTRENDERER_GFX_MUSHROOM)
                               : (col & 8U)      ? REF(TESTRENDERER_GFX_FLOWER_R)
                                                 : REF(TESTRENDERER_GFX_FLOWER_B);
            s_fg[n++] = spr((int16_t)(sx + 4), (int16_t)(gy - deco->h), 9U, deco, 0U);
        }
        if (col % 9U == 6U)
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_SAPLING), 0U);
        }
        if (col % 6U == 0U) /* fence rail along the ground */
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 16), 7U, REF(TESTRENDERER_GFX_FENCE), 0U);
        }

        /* --- landmarks (use the big & structural tiles) --- */
        if (col % 24U == 5U) /* a cottage: stone base, plaster walls, window, door, roof, lantern */
        {
            int16_t hx = sx, hy = (int16_t)(gy - 48);
            const Tile *rf = ((col / 24U) & 1U) ? REF(TESTRENDERER_GFX_ROOF_BLUE)
                                                : REF(TESTRENDERER_GFX_ROOF_RED);
            s_fg[n++] = spr(hx, (int16_t)(hy + 32), 10U, REF(TESTRENDERER_GFX_STONE), 0U);                  /* foundation */
            s_fg[n++] = spr((int16_t)(hx + 16), (int16_t)(hy + 32), 10U, REF(TESTRENDERER_GFX_WOOD_V), 0U); /* door */
            s_fg[n++] = spr((int16_t)(hx + 32), (int16_t)(hy + 32), 10U, REF(TESTRENDERER_GFX_STONE), 0U);
            s_fg[n++] = spr(hx, (int16_t)(hy + 16), 10U, REF(TESTRENDERER_GFX_PLASTER), 0U); /* upper wall */
            s_fg[n++] = spr((int16_t)(hx + 16), (int16_t)(hy + 16), 10U, REF(TESTRENDERER_GFX_WINDOW), 0U);
            s_fg[n++] = spr((int16_t)(hx + 32), (int16_t)(hy + 16), 10U, REF(TESTRENDERER_GFX_PLASTER), 0U);
            s_fg[n++] = spr(hx, hy, 11U, rf, 0U); /* gable roof */
            s_fg[n++] = spr((int16_t)(hx + 16), hy, 11U, rf, 0U);
            s_fg[n++] = spr((int16_t)(hx + 32), hy, 11U, rf, SPRITE_FLIP_H);
            s_fg[n++] = spr((int16_t)(hx + 30), (int16_t)(hy + 30), 12U, REF(TESTRENDERER_GFX_LANTERN), 0U);
            s_fg[n++] = spr((int16_t)(hx + 16), gy, 9U, REF(TESTRENDERER_GFX_WOOD_H), 0U); /* porch step */
        }
        else if (col % 24U == 16U) /* a stone tower (32x48) on a marble terrace, flying a banner */
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 48), 10U, REF(TESTRENDERER_GFX_TOWER), 0U);
            s_fg[n++] = spr((int16_t)(sx + 30), (int16_t)(gy - 46), 18U, REF(TESTRENDERER_GFX_BANNER), 0U);
            s_fg[n++] = spr((int16_t)(sx + 6), (int16_t)(gy - 30), 19U, REF(TESTRENDERER_GFX_TORCH), 0U);
            s_fg[n++] = spr(sx, gy, 9U, REF(TESTRENDERER_GFX_MARBLE), 0U); /* terrace */
            s_fg[n++] = spr((int16_t)(sx + 16), gy, 9U, REF(TESTRENDERER_GFX_FLOOR), 0U);
        }
        else if (col % 30U == 22U) /* a brick rampart + big wall section + archway gate */
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_BRICK_DARK), 0U);
            s_fg[n++] = spr(sx, (int16_t)(gy - 32), 10U, REF(TESTRENDERER_GFX_STONEWALL), 0U);
            s_fg[n++] = spr((int16_t)(sx + 8), (int16_t)(gy - 32), 12U, REF(TESTRENDERER_GFX_ARCHWAY), 0U);
            s_fg[n++] = spr((int16_t)(sx + 48), (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_BRICK), 0U);
        }
        else if (col % 20U == 13U) /* a well on mossy stone, with a barrel & crate beside it */
        {
            s_fg[n++] = spr((int16_t)(sx - 16), (int16_t)(gy - 16), 8U, REF(TESTRENDERER_GFX_MOSSY), 0U);
            s_fg[n++] = spr(sx, (int16_t)(gy - 24), 9U, REF(TESTRENDERER_GFX_WELL), 0U);
            s_fg[n++] = spr((int16_t)(sx + 32), (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_BARREL), 0U);
            s_fg[n++] = spr((int16_t)(sx + 48), (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_CRATE), 0U);
        }
        else if (col % 16U == 9U) /* a boulder, a sign, a chest */
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 30), 9U, REF(TESTRENDERER_GFX_BOULDER), 0U);
            s_fg[n++] = spr((int16_t)(sx + 34), (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_SIGN), 0U);
            s_fg[n++] = spr((int16_t)(sx + 50), (int16_t)(gy - 16), 9U, REF(TESTRENDERER_GFX_CHEST), 0U);
        }

        /* --- enemies & critters (bob/drift in real time) --- */
        if (col % 8U == 4U)
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 16), 20U, REF(TESTRENDERER_GFX_SLIME), (col & 4U) ? SPRITE_FLIP_H : 0U);
        }
        if (col % 13U == 7U)
        {
            s_fg[n++] = spr(sx, (int16_t)(gy - 24), 20U, REF(TESTRENDERER_GFX_SKELETON), 0U);
        }
        // if (col % 19U == 10U) /* the boss (48x48, big) roams occasionally */
        // {
        //     s_fg[n++] = spr((int16_t)(sx - 16), (int16_t)(gy - 48), 21U, REF(TESTRENDERER_GFX_BOSS), 0U);
        // }
        if (col % 6U == 2U) /* a bird gliding in the sky */
        {
            int16_t by = (int16_t)(24 + bob((uint32_t)col * 40U, 900U, 10));
            s_fg[n++] = spr(sx, by, 22U, REF(TESTRENDERER_GFX_BIRD), (col & 2U) ? SPRITE_FLIP_H : 0U);
        }
        if (col % 9U == 0U) /* a bat */
        {
            s_fg[n++] = spr(sx, (int16_t)(40 + bob((uint32_t)col * 70U, 600U, 12)), 22U,
                            REF(TESTRENDERER_GFX_BAT), 0U);
        }
        if (biomeOf(col) == 5U && col % 3U == 0U) /* a fish skimming a water band */
        {
            s_fg[n++] = spr(sx, (int16_t)(gy + 2 + bob((uint32_t)col * 50U, 700U, 3)), 6U,
                            REF(TESTRENDERER_GFX_FISH), (col & 2U) ? SPRITE_FLIP_H : 0U);
        }

        /* --- floating collectibles (bob) --- */
        if (col % 4U == 0U)
        {
            const Tile *it = (col % 12U == 0U)  ? REF(TESTRENDERER_GFX_GEM)
                             : (col % 8U == 0U) ? REF(TESTRENDERER_GFX_POTION)
                                                : REF(TESTRENDERER_GFX_COIN);
            int16_t iy = (int16_t)(gy - 28 + bob((uint32_t)col * 64U, 420U, 6));
            s_fg[n++] = spr((int16_t)(sx + 4), iy, 14U, it, 0U);
        }
    }

    /* The hero stands centred and bobs while the world scrolls past him. */
    if (n < LAYER_CAP)
    {
        int16_t hgy = (int16_t)(terrainTop((uint16_t)(s_camera_x / TILE) + COLS / 2) * TILE);
        s_fg[n++] = spr((int16_t)((RENDERER_WIDTH - TILE) / 2),
                        (int16_t)(hgy - 16 - bob(0U, 280U, 3)), 30U, REF(TESTRENDERER_GFX_HERO), 0U);
    }
    return n;
}

/* LAYER_UI: the HUD (hearts / coins / gems) and the FPS/load readouts, all at high z
 * so the load-sweep filler never covers them. */
static uint16_t buildOverlay(void)
{
    uint16_t n = 0U;
    char line[24];

    for (uint8_t i = 0U; i < 6U; i++) /* hearts top-right */
    {
        s_ui[n++] = spr((int16_t)(210 + i * 10), 3, 55U, REF(TESTRENDERER_GFX_HEART), 0U);
    }
    for (uint8_t i = 0U; i < 6U; i++) /* coins + a key below */
    {
        s_ui[n++] = spr((int16_t)(210 + i * 10), 14, 55U, REF(TESTRENDERER_GFX_COIN), 0U);
    }
    s_ui[n++] = spr(296, 14, 55U, REF(TESTRENDERER_GFX_KEY), 0U);
    s_ui[n++] = spr(210, 25, 55U, REF(TESTRENDERER_GFX_GEM), 0U);
    s_ui[n++] = spr(222, 25, 55U, REF(TESTRENDERER_GFX_RING), 0U);
    s_ui[n++] = spr(238, 24, 55U, REF(TESTRENDERER_GFX_STAR), 0U);

    /* Readouts via the console text path — no glyph sprites in this game's arrays. */
    snprintf(line, sizeof(line), "FPS %u", (unsigned)s_fps_cur);
    rendererDrawText(LAYER_UI, 4, 4, 60U, FONT_8x8, 1U, COL_AMBER, line);
    snprintf(line, sizeof(line), "MIN %u", (unsigned)s_fps_min);
    rendererDrawText(LAYER_UI, 4, 14, 60U, FONT_8x8, 1U, COL_AMBER, line);
    snprintf(line, sizeof(line), "AVG %u", (unsigned)s_fps_avg);
    rendererDrawText(LAYER_UI, 4, 24, 60U, FONT_8x8, 1U, COL_AMBER, line);
    snprintf(line, sizeof(line), "MAX %u", (unsigned)s_fps_max);
    rendererDrawText(LAYER_UI, 4, 34, 60U, FONT_8x8, 1U, COL_AMBER, line);
    snprintf(line, sizeof(line), "LOAD %u%% %u", (unsigned)s_load_pct, (unsigned)s_sprite_target);
    rendererDrawText(LAYER_UI, 4, 48, 60U, FONT_5x5, 1U, COL_AMBER, line);
    rendererDrawText(LAYER_UI, 4, 226, 60U, FONT_5x5, 1U, COL_CYAN, "SB2 EXIT");
    return n;
}

/* Append up to `want` drifting collectibles to a layer (capped at LAYER_CAP). They
 * cycle through several small (8x8) sprites so the decoded-tile cache keeps missing,
 * and their y is scattered across the screen so no 16-line chunk overflows its bin.
 * A handful are H-flipped to keep the flipped blit on the hot path. */
static uint16_t addFiller(Sprite *arr, uint16_t n, uint16_t want, uint8_t z, uint16_t seed)
{
    static const uint32_t kinds[6] = {
        TESTRENDERER_GFX_COIN, TESTRENDERER_GFX_GEM, TESTRENDERER_GFX_STAR,
        TESTRENDERER_GFX_HEART, TESTRENDERER_GFX_BUTTERFLY, TESTRENDERER_GFX_FLOWER_B};
    const uint32_t drift = (s_anim_ms * 50U) / 1000U; /* 50 px/s, frame-rate independent */
    for (uint16_t k = 0U; k < want && n < LAYER_CAP; k++, n++)
    {
        const uint32_t i = (uint32_t)seed + k;
        int16_t x = (int16_t)((i * 53U + drift) % RENDERER_WIDTH);
        int16_t y = (int16_t)((i * 97U) % RENDERER_HEIGHT);
        if (x < READOUT_W && y < READOUT_H) /* keep the readout panel clear */
        {
            y = (int16_t)(y + READOUT_H);
        }
        arr[n] = spr(x, y, z, REF(kinds[i % 6U]), (i & 3U) == 0U ? SPRITE_FLIP_H : 0U);
    }
    return n;
}

/* Rebuild and submit the whole visible window, then top it up with filler so the
 * total tracks s_sprite_target. Filler pours into BG, then FG, then UI — each up to
 * its 350 cap — so at 100% all three layers are maxed (3 x 350 = 1050). */
static void buildVisibleScene(void)
{
    const uint16_t first_col = (uint16_t)(s_camera_x / TILE);
    uint16_t bg_n = buildBackground(first_col);
    uint16_t fg_n = buildForeground(first_col);
    uint16_t ui_n = buildOverlay();

    const uint16_t have = (uint16_t)(bg_n + fg_n + ui_n);
    uint16_t filler = (s_sprite_target > have) ? (uint16_t)(s_sprite_target - have) : 0U;
    uint16_t take;

    take = (filler < (uint16_t)(LAYER_CAP - bg_n)) ? filler : (uint16_t)(LAYER_CAP - bg_n);
    bg_n = addFiller(s_bg, bg_n, take, 3U, 2000U);
    filler -= take;
    take = (filler < (uint16_t)(LAYER_CAP - fg_n)) ? filler : (uint16_t)(LAYER_CAP - fg_n);
    fg_n = addFiller(s_fg, fg_n, take, 25U, 0U);
    filler -= take;
    take = (filler < (uint16_t)(LAYER_CAP - ui_n)) ? filler : (uint16_t)(LAYER_CAP - ui_n);
    ui_n = addFiller(s_ui, ui_n, take, 50U, 1000U);

    rendererSubmitLayer(LAYER_BG, s_bg, bg_n);
    rendererSubmitLayer(LAYER_FG, s_fg, fg_n);
    rendererSubmitLayer(LAYER_UI, s_ui, ui_n);
}

/* Load all 64 tile assets from the .pak. Most stream into the CCM arena; three small
 * 4bpp actors go into a GAME_RAM buffer (to exercise both pools). The per-layer
 * Sprite arrays are carved from the CCM arena after the tiles. */
static bool loadAssets(void)
{
    uint8_t *ccm = ASSET_ARENA_START;
    uint8_t *gram = s_gram_assets;
    for (uint32_t id = 1U; id <= TILE_COUNT; id++)
    {
        const bool to_gram = (id == TESTRENDERER_GFX_HERO || id == TESTRENDERER_GFX_SLIME ||
                              id == TESTRENDERER_GFX_BAT);
        uint8_t **cur = to_gram ? &gram : &ccm;
        const uint8_t *end = to_gram ? (s_gram_assets + sizeof(s_gram_assets)) : ASSET_ARENA_END;
        if (!loadTile(id, &s_tile[id - 1U], cur, end))
        {
            return false;
        }
    }
    ccm = (uint8_t *)(((uintptr_t)ccm + 3U) & ~(uintptr_t)3U);
    s_bg = (Sprite *)ccm;
    s_fg = s_bg + LAYER_CAP;
    s_ui = s_fg + LAYER_CAP;
    return true;
}

static void gameInit(void)
{
    if (!loadAssets())
    {
        gameLog("renderer test: asset load failed");
        gameExit();
    }
    rendererSetBackground(RGB(150, 200, 240)); /* open sky behind the horizon */
    gameLog("renderer test: 64 sprites (32 opaque/32 transparent, 8x8..64x32), "
            "load swept 50-100%% of %u; SB2 to exit",
            (unsigned)SPRITE_BUDGET);
}

static void gameUpdate(void)
{
    InputState in;
    inputGetState(&in);
    if (in.special2.pressed)
    {
        gameExit();
    }
    const uint32_t dt_us = getDeltaTimeUs();
    s_anim_ms += dt_us / 1000U;
    s_scroll_px += SCROLL_PX_PER_SEC * (dt_us * 1e-6f);
    while (s_scroll_px >= (float)WORLD_PERIOD_PX)
    {
        s_scroll_px -= (float)WORLD_PERIOD_PX;
    }
    s_camera_x = (int)s_scroll_px;

    updateLoad();
    fpsSample();
    buildVisibleScene();
}

static void gameRender(void)
{
    rendererRender();
}

DECLARE_GAME_HEADER(gameInit, gameUpdate, gameRender);
