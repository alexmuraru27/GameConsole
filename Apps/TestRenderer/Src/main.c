#include "game_console_api.h"
#include "gfx_asset.h"               /* GfxAssetHeader / GFX1 format (tools/graphics) */
#include "TestRendererAssetEnum.h"   /* TESTRENDERER_GFX_* ids (packer-generated) */
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/*
 * TestRenderer — a renderer benchmark, shipped as an ordinary loadable game.
 *
 * It was the console's old in-tree perf harness (renderer_testing.c); turning it
 * into a game removes the special-case from the OS and exercises the exact path a
 * real game takes: it runs unprivileged, MPU-confined, reaching the renderer only
 * through SVC syscalls, and it streams its graphics from a .pak like any game.
 *
 * It is an endless auto-scroller with almost no game logic — the world slides past
 * and repeats forever — over a base scene heavy on all three layers (a full BG tile
 * grid, a dense FG of terrain + structures + props + enemies, a busy UI HUD). On
 * top of that the *load is swept continuously between 50% and 100% of the renderer's
 * sprite budget* (3 layers x 350 = 1050 sprites): a slow triangle wave adds a
 * drifting swarm of filler sprites, spread across the screen so no 16-line chunk
 * overflows the compositor's per-chunk bin. The OS runs games uncapped, so the
 * on-screen counters (current / min / avg / max FPS) show how throughput tracks the
 * load. Special Button 2 quits back to the console.
 *
 * Memory: the tile graphics ship in TestRenderer.pak and are streamed once at init.
 * To exercise both asset pools, the load is split — the world + HUD tiles go into
 * the CCM asset arena, the 4bpp actor sprites (hero, slime) into a small GAME_RAM
 * buffer. The three per-layer Sprite arrays (1050 * 20 B = 21 KB) also live in the
 * CCM arena (GAME_RAM has no room): the renderer reads sprite data with the CPU,
 * for which CCM is zero-wait, and the arena is RW for the game under the MPU.
 */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define TILE 16
#define COLS (RENDERER_WIDTH / TILE)  /* 20 on-screen columns */
#define ROWS (RENDERER_HEIGHT / TILE) /* 15 rows */
#define VIS_COLS (COLS + 1)           /* one extra column for the partial edge tile */
#define SCROLL_SPEED 1                /* world pixels per frame; endless */

/* The renderer caps each layer at 350 sprites; the whole-frame budget is 3x that.
 * The load sweep targets a fraction of this budget. */
#define LAYER_CAP 350U
#define SPRITE_BUDGET (3U * LAYER_CAP) /* 1050 */
#define LOAD_MIN_PCT 50U
#define LOAD_MAX_PCT 100U
#define SWEEP_PERIOD_MS 8000U /* one 50 -> 100 -> 50 sweep every 8 s (real time) */

/* The world repeats every LCM(8,5,6,3) = 120 columns, so wrapping the camera by
 * that many tiles keeps the procedural structures seamless while bounding the
 * integer. (Every structure period below divides 120.) */
#define WORLD_PERIOD_COLS 120
#define WORLD_PERIOD_PX (WORLD_PERIOD_COLS * TILE)

/* Top-left rectangle holding the FPS/load readouts. Filler is kept out of it so
 * the small text stays legible against an otherwise busy screen — it is wider and
 * taller than the text (which spans ~x[4..88], y[4..55]) so no 16px filler tile
 * starting outside can creep in over a glyph. */
#define READOUT_W 96
#define READOUT_H 62

/* ---- Font-colour palettes (slot 0 transparent, 1-3 the ink). Tile palettes come
 * from the loaded assets (system-palette indices mapped to RGB565). ---- */
static const uint16_t s_pal_font_white[4] = {0x0000, 0xFFFF, 0xFFFF, 0xFFFF};
static const uint16_t s_pal_font_green[4] = {0x0000, 0x07E0, 0x07E0, 0x07E0};
static const uint16_t s_pal_font_amber[4] = {0x0000, 0xFD20, 0xFD20, 0xFD20};
static const uint16_t s_pal_font_cyan[4] = {0x0000, 0x07FF, 0x07FF, 0x07FF};

/* One decoded tile: a pointer into its loaded GfxAsset blob plus the RGB565 palette
 * (system indices resolved through rendererSystemColor) and the sprite flags
 * (format + opacity) the asset carries. Built once at init, referenced every frame. */
typedef struct
{
    const uint8_t *pixels;
    uint16_t palette[16];
    uint8_t flags;
} Tile;

enum
{
    T_BRICK, T_STONE, T_GROUND, T_TORCH, T_COIN, T_HEART, T_HERO, T_SLIME, T_COUNT
};
static Tile s_tile[T_COUNT];

/* Asset pools. The CCM arena (ASSET_ARENA_START..END from the ConsoleAPI — 64 KB,
 * otherwise idle since this game's tile blobs are tiny) holds the world/HUD tile
 * blobs followed by the three per-layer Sprite arrays; a small GAME_RAM buffer
 * holds the 4bpp actor blobs. See the file header. */
static uint8_t s_gram_assets[384] __attribute__((aligned(4)));
static Sprite *s_bg; /* [LAYER_CAP], in the CCM arena */
static Sprite *s_fg; /* [LAYER_CAP], in the CCM arena */
static Sprite *s_ui; /* [LAYER_CAP], in the CCM arena */

/* World/animation state. Almost no game logic: a free-running frame counter for
 * cheap bobbing, and a camera that slides right forever (wrapped to stay bounded). */
static uint32_t s_tick;
static int s_camera_x;

/* Load-sweep state, updated once per frame from real time. */
static uint16_t s_load_pct;      /* 50..100, displayed */
static uint16_t s_sprite_target; /* target total sprites for this frame */

/* ---- FPS measurement -------------------------------------------------------
 * Frames are timed with getSysTime() (1 ms tick). The "current" value updates 4x
 * a second so it tracks the load sweep; min/avg/max latch once per second. */
static uint16_t s_fps_cur, s_fps_min, s_fps_avg, s_fps_max; /* displayed */
static uint32_t s_last_frame_ms;                            /* previous frame timestamp */
static uint32_t s_cur_start_ms, s_cur_frames;               /* 250 ms "current" sub-window */
static uint32_t s_win_start_ms, s_win_frames;               /* 1 s min/avg/max window */
static uint16_t s_win_fps_lo, s_win_fps_hi;                 /* instantaneous extremes this window */

/* All tiles are 16x16; flags/pixels/palette come from the loaded asset. */
static Sprite tileAt(int16_t x, int16_t y, uint8_t z, const Tile *t)
{
    return (Sprite){.x = x, .y = y, .w = TILE, .h = TILE, .z = z,
                    .flags = t->flags, .pixels = t->pixels, .palette = t->palette};
}

/* A 0..amp..0 triangle wave from the frame counter, for cheap bobbing. */
static int bob(uint32_t phase, uint32_t period, int amp)
{
    uint32_t p = ((s_tick / 4U) + phase) % (period * 2U);
    int v = (int)(p < period ? p : period * 2U - p); /* 0..period */
    return (int)((long)v * amp / (long)period);      /* 0..amp */
}

/* Render a C string into consecutive UI sprites. Returns the next free index.
 * Glyph pixels point straight at console flash (fontGet), so no pool is needed. */
static uint16_t draw_text(Sprite *ui, uint16_t idx, FontSize size,
                          int16_t x, int16_t y, uint8_t z,
                          const uint16_t *palette, const char *text)
{
    uint16_t glyphW = fontGlyphW(size);
    uint16_t glyphH = fontGlyphH(size);

    for (const char *scan = text; *scan != '\0' && idx < LAYER_CAP; scan++)
    {
        uint8_t ascii = (uint8_t)*scan;
        if (ascii >= 0x20U && ascii <= 0x7EU)
        {
            const uint8_t *pixels;
            fontGet(ascii, size, &pixels);
            ui[idx] = (Sprite){.x = x, .y = y, .w = glyphW, .h = glyphH, .z = z,
                               .flags = 0U, .pixels = pixels, .palette = palette};
            idx++;
        }
        x = (int16_t)(x + glyphW + 1U);
    }
    return idx;
}

/* Stream one tile asset from the bound .pak into the arena at *cursor (bump
 * allocated, 4-aligned, bounded by end), decode its header, and resolve its
 * system-palette indices to RGB565. Returns false on any failure. */
static bool loadTile(uint32_t asset_id, Tile *out, uint8_t **cursor, const uint8_t *end)
{
    AssetMetaData meta;
    if (assetLoaderGetAssetMetadata(asset_id, &meta) != 0U)
    {
        return false;
    }
    uint8_t *blob = (uint8_t *)(((uintptr_t)*cursor + 3U) & ~(uintptr_t)3U);
    if (blob + meta.size > end)
    {
        return false;
    }
    if (assetLoaderGetAssetData(asset_id, blob, meta.size) != 0U)
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
    const uint8_t *index_palette = pixels + h->dataSize;
    const uint32_t colors = (h->format == GFX_FMT_4BPP) ? 16U : 4U;
    for (uint32_t i = 0U; i < colors; i++)
    {
        out->palette[i] = rendererSystemColor(index_palette[i]);
    }
    out->pixels = pixels;
    out->flags = (uint8_t)h->flags; /* GFX_FLAG_* mirror the renderer's SpriteFlags */
    return true;
}

/* Derive this frame's load percentage and sprite target from a real-time triangle
 * wave sweeping LOAD_MIN_PCT -> LOAD_MAX_PCT -> LOAD_MIN_PCT. */
static void updateLoad(void)
{
    const uint32_t phase = getSysTime() % SWEEP_PERIOD_MS;
    const uint32_t half = SWEEP_PERIOD_MS / 2U;
    const uint32_t tri = (phase < half) /* 0..1000..0 */
                             ? (phase * 1000U / half)
                             : (1000U - (phase - half) * 1000U / half);
    s_load_pct = (uint16_t)(LOAD_MIN_PCT + (LOAD_MAX_PCT - LOAD_MIN_PCT) * tri / 1000U);
    s_sprite_target = (uint16_t)((uint32_t)SPRITE_BUDGET * s_load_pct / 100U);
}

/* Sample one frame's duration; refresh the current FPS (4 Hz) and, once per
 * second, the min/avg/max window. */
static void fpsSample(void)
{
    const uint32_t now = getSysTime();

    if (s_last_frame_ms != 0U)
    {
        uint32_t dt = now - s_last_frame_ms;
        if (dt == 0U)
        {
            dt = 1U; /* sub-millisecond frame: clamp so the division stays sane */
        }
        const uint16_t inst = (uint16_t)(1000U / dt);
        if (inst < s_win_fps_lo)
        {
            s_win_fps_lo = inst;
        }
        if (inst > s_win_fps_hi)
        {
            s_win_fps_hi = inst;
        }
    }
    s_last_frame_ms = now;
    s_cur_frames++;
    s_win_frames++;

    if (now - s_cur_start_ms >= 250U)
    {
        s_fps_cur = (uint16_t)(s_cur_frames * 1000U / (now - s_cur_start_ms));
        s_cur_start_ms = now;
        s_cur_frames = 0U;
    }
    if (now - s_win_start_ms >= 1000U)
    {
        s_fps_avg = (uint16_t)(s_win_frames * 1000U / (now - s_win_start_ms));
        s_fps_min = s_win_fps_lo;
        s_fps_max = s_win_fps_hi;
        gameLog("renderer test: load=%u%% (%u spr)  fps cur=%u min=%u avg=%u max=%u",
                (unsigned)s_load_pct, (unsigned)s_sprite_target, (unsigned)s_fps_cur,
                (unsigned)s_fps_min, (unsigned)s_fps_avg, (unsigned)s_fps_max);
        s_win_start_ms = now;
        s_win_frames = 0U;
        s_win_fps_lo = 0xFFFFU;
        s_win_fps_hi = 0U;
    }
}

/* Procedural terrain top row for an absolute column — gentle rolling hills that
 * repeat with WORLD_PERIOD_COLS. Ground tiles stack from here down to the floor. */
static uint8_t terrainTop(uint16_t col)
{
    static const uint8_t profile[8] = {13U, 12U, 11U, 12U, 13U, 12U, 11U, 10U};
    return profile[(col / 2U) % 8U];
}

/* LAYER_BG: a full screen-filling brick grid behind everything. Returns its count. */
static uint16_t buildBackground(uint16_t first_col)
{
    uint16_t nb = 0U;
    for (uint16_t vc = 0U; vc < VIS_COLS; vc++)
    {
        int16_t sx = (int16_t)((int)(first_col + vc) * TILE - s_camera_x);
        for (uint8_t row = 0U; row < ROWS; row++)
        {
            s_bg[nb++] = tileAt(sx, (int16_t)(row * TILE), 0U, &s_tile[T_BRICK]);
        }
    }
    return nb; /* VIS_COLS * ROWS = 315 */
}

/* LAYER_FG: dense midground over the bricks — terrain, towers, platforms, torches,
 * floating coins and patrolling slimes, plus the bobbing hero centred on screen. */
static uint16_t buildForeground(uint16_t first_col)
{
    uint16_t nf = 0U;

    for (uint16_t vc = 0U; vc < VIS_COLS && nf < LAYER_CAP - 1U; vc++)
    {
        uint16_t col = first_col + vc;
        int16_t sx = (int16_t)((int)col * TILE - s_camera_x);

        for (uint8_t r = terrainTop(col); r < ROWS; r++) /* hilly ground column */
        {
            s_fg[nf++] = tileAt(sx, (int16_t)(r * TILE), 1U, &s_tile[T_GROUND]);
        }
        if (col % 8U == 0U || col % 8U == 1U) /* a stone tower every 8 columns */
        {
            for (uint8_t r = 6U; r <= 13U; r++)
            {
                s_fg[nf++] = tileAt(sx, (int16_t)(r * TILE), 2U, &s_tile[T_STONE]);
            }
            if (col % 8U == 0U) /* torch on the tower */
            {
                s_fg[nf++] = tileAt(sx, (int16_t)(7 * TILE), 10U, &s_tile[T_TORCH]);
            }
        }
        if (col % 5U == 2U) /* floating stone platform */
        {
            s_fg[nf++] = tileAt(sx, (int16_t)(8 * TILE), 3U, &s_tile[T_STONE]);
        }
        if (col % 3U == 0U) /* bobbing coin */
        {
            int16_t cy = (int16_t)(5 * TILE + bob(col, 6U, TILE));
            s_fg[nf++] = tileAt(sx, cy, 12U, &s_tile[T_COIN]);
        }
        if (col % 6U == 4U) /* a slime patrolling the terrain top */
        {
            int16_t gy = (int16_t)((terrainTop(col) - 1U) * TILE);
            s_fg[nf++] = tileAt(sx, gy, 20U, &s_tile[T_SLIME]);
        }
    }

    /* The hero sits centred and just bobs — the world scrolls past him. */
    s_fg[nf++] = tileAt((int16_t)((RENDERER_WIDTH - TILE) / 2),
                        (int16_t)(10 * TILE - bob(0U, 4U, 4)), 30U, &s_tile[T_HERO]);
    return nf;
}

/* LAYER_UI: the HUD (hearts + coins) and the readouts — current/min/avg/max FPS and
 * the live load. Text sits at high z so the load-sweep filler never covers it. */
static uint16_t buildOverlay(void)
{
    uint16_t nu = 0U;
    char line[24];

    for (uint8_t i = 0U; i < 8U; i++) /* hearts top-right */
    {
        s_ui[nu++] = tileAt((int16_t)(196 + i * 15), 2, 55U, &s_tile[T_HEART]);
    }
    for (uint8_t i = 0U; i < 8U; i++) /* coins below the hearts */
    {
        s_ui[nu++] = tileAt((int16_t)(196 + i * 15), 20, 55U, &s_tile[T_COIN]);
    }

    snprintf(line, sizeof(line), "FPS %u", (unsigned)s_fps_cur);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 4, 60U, s_pal_font_amber, line);
    snprintf(line, sizeof(line), "MIN %u", (unsigned)s_fps_min);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 14, 60U, s_pal_font_green, line);
    snprintf(line, sizeof(line), "AVG %u", (unsigned)s_fps_avg);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 24, 60U, s_pal_font_white, line);
    snprintf(line, sizeof(line), "MAX %u", (unsigned)s_fps_max);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 34, 60U, s_pal_font_cyan, line);

    snprintf(line, sizeof(line), "LOAD %u%% %u", (unsigned)s_load_pct, (unsigned)s_sprite_target);
    nu = draw_text(s_ui, nu, FONT_5x5, 4, 50, 60U, s_pal_font_amber, line);
    nu = draw_text(s_ui, nu, FONT_5x5, 4, 226, 60U, s_pal_font_cyan, "SB2 EXIT");
    return nu;
}

/* Append up to `want` drifting filler coins to a layer (capped at LAYER_CAP).
 * Positions are scattered across the whole screen — crucially the y is spread so
 * the renderer's per-chunk bin (16 scanlines, cap 200) never overflows — and drift
 * horizontally with the frame counter. `seed` offsets each layer's swarm. */
static uint16_t addFiller(Sprite *arr, uint16_t n, uint16_t want, uint8_t z, uint16_t seed)
{
    for (uint16_t k = 0U; k < want && n < LAYER_CAP; k++, n++)
    {
        const uint32_t i = (uint32_t)seed + k;
        int16_t x = (int16_t)((i * 53U + s_tick) % RENDERER_WIDTH);
        int16_t y = (int16_t)((i * 97U) % RENDERER_HEIGHT);
        if (x < READOUT_W && y < READOUT_H) /* shove it below the readout panel, keeping the count exact */
        {
            y = (int16_t)(y + READOUT_H);
        }
        arr[n] = tileAt(x, y, z, &s_tile[T_COIN]);
    }
    return n;
}

/* Rebuild and submit the whole visible window, then top it up with filler so the
 * total tracks s_sprite_target. Filler pours into FG, then UI, then BG — each up to
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

    take = (filler < (uint16_t)(LAYER_CAP - fg_n)) ? filler : (uint16_t)(LAYER_CAP - fg_n);
    fg_n = addFiller(s_fg, fg_n, take, 15U, 0U);
    filler -= take;
    take = (filler < (uint16_t)(LAYER_CAP - ui_n)) ? filler : (uint16_t)(LAYER_CAP - ui_n);
    ui_n = addFiller(s_ui, ui_n, take, 10U, 1000U);
    filler -= take;
    take = (filler < (uint16_t)(LAYER_CAP - bg_n)) ? filler : (uint16_t)(LAYER_CAP - bg_n);
    bg_n = addFiller(s_bg, bg_n, take, 1U, 2000U);

    rendererSubmitLayer(LAYER_BG, s_bg, bg_n);
    rendererSubmitLayer(LAYER_FG, s_fg, fg_n);
    rendererSubmitLayer(LAYER_UI, s_ui, ui_n);
}

/* Load all tile assets from the .pak and carve the per-layer Sprite arrays. The
 * world/HUD tiles stream into the CCM arena, the 4bpp actors into a GAME_RAM buffer
 * (exercising both pools); the Sprite arrays follow the tiles in the CCM arena. */
static bool loadAssets(void)
{
    uint8_t *ccm = ASSET_ARENA_START;
    uint8_t *gram = s_gram_assets;
    bool ok = true;

    ok = ok && loadTile(TESTRENDERER_GFX_BRICK, &s_tile[T_BRICK], &ccm, ASSET_ARENA_END);
    ok = ok && loadTile(TESTRENDERER_GFX_STONE, &s_tile[T_STONE], &ccm, ASSET_ARENA_END);
    ok = ok && loadTile(TESTRENDERER_GFX_GROUND, &s_tile[T_GROUND], &ccm, ASSET_ARENA_END);
    ok = ok && loadTile(TESTRENDERER_GFX_TORCH, &s_tile[T_TORCH], &ccm, ASSET_ARENA_END);
    ok = ok && loadTile(TESTRENDERER_GFX_COIN, &s_tile[T_COIN], &ccm, ASSET_ARENA_END);
    ok = ok && loadTile(TESTRENDERER_GFX_HEART, &s_tile[T_HEART], &ccm, ASSET_ARENA_END);
    /* the 4bpp actor sprites stream into GAME_RAM instead, to use both pools */
    ok = ok && loadTile(TESTRENDERER_GFX_HERO, &s_tile[T_HERO], &gram, s_gram_assets + sizeof(s_gram_assets));
    ok = ok && loadTile(TESTRENDERER_GFX_SLIME, &s_tile[T_SLIME], &gram, s_gram_assets + sizeof(s_gram_assets));
    if (!ok)
    {
        return false;
    }

    /* Carve the three per-layer Sprite arrays from the CCM arena, after the tiles.
     * 6 tile blobs (~0.5 KB) + 3 * 350 * 20 B (21 KB) fits the 64 KB arena easily. */
    ccm = (uint8_t *)(((uintptr_t)ccm + 3U) & ~(uintptr_t)3U);
    s_bg = (Sprite *)ccm;
    s_fg = s_bg + LAYER_CAP;
    s_ui = s_fg + LAYER_CAP;
    return true;
}

/*
 * Game lifecycle. The OS runs the C-runtime bootstrap, then init once, then
 * update/render every frame back to back (uncapped). This benchmark never paces
 * itself — running flat out is the whole point.
 */

static void gameInit(void)
{
    if (!loadAssets())
    {
        gameLog("renderer test: asset load failed");
        gameExit(); /* nothing to draw without the tiles */
    }

    rendererSetBackground(RGB(20, 18, 30)); /* shows only where nothing draws */

    s_cur_start_ms = getSysTime();
    s_win_start_ms = s_cur_start_ms;
    s_win_fps_lo = 0xFFFFU;
    s_win_fps_hi = 0U;
    gameLog("renderer test: endless scroller, load swept 50-100%% of %u sprites; SB2 to exit",
            (unsigned)SPRITE_BUDGET);
}

static void gameUpdate(void)
{
    if (joystickGetSpecialBtn2())
    {
        gameExit(); /* Special Button 2 returns to the console OS (does not return) */
    }

    /* The only "game logic": advance time and slide the camera, wrapped so the
     * procedural world is seamless and the integer stays bounded. */
    s_tick++;
    s_camera_x += SCROLL_SPEED;
    if (s_camera_x >= WORLD_PERIOD_PX)
    {
        s_camera_x -= WORLD_PERIOD_PX;
    }

    updateLoad();
    fpsSample();
    buildVisibleScene();
}

static void gameRender(void)
{
    rendererRender();
}

/* Emit the binary header: magic + ABI version + the runtime stubs are filled in
 * for us; we just name our three callbacks. */
DECLARE_GAME_HEADER(gameInit, gameUpdate, gameRender);
