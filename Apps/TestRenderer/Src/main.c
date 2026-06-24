#include "game_console_api.h"
#include <string.h>
#include <stdio.h>

/*
 * TestRenderer — a renderer benchmark, shipped as an ordinary loadable game.
 *
 * It was the console's old in-tree perf harness (renderer_testing.c); turning it
 * into a game removes the special-case from the OS and exercises the exact path a
 * real game takes: it runs unprivileged, MPU-confined, reaching the renderer only
 * through SVC syscalls.
 *
 * It is an endless auto-scroller with almost no game logic — the world just slides
 * left at a fixed rate and repeats forever — but it is deliberately *heavy on
 * sprites across all three layers*, which is the point: it stresses the scanline
 * compositor with high overdraw (a full BG tile grid, a dense FG of terrain +
 * structures + props + enemies on top of it, and a busy UI HUD over both).
 *
 * Opaque tiles keep SPRITE_OPAQUE so the renderer's decoded-tile cache applies;
 * scrolling moves their screen x but the cache is keyed by graphic + palette, so
 * it survives the motion. The OS runs games uncapped, so the three live counters
 * top-left — min / avg / max FPS over a one-second window — report raw renderer
 * throughput. Special Button 2 quits back to the console.
 */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define TILE 16
#define COLS (RENDERER_WIDTH / TILE)  /* 20 on-screen columns */
#define ROWS (RENDERER_HEIGHT / TILE) /* 15 rows */
#define VIS_COLS (COLS + 1)           /* one extra column for the partial edge tile */
#define SCROLL_SPEED 1                /* world pixels per frame; endless */

/* The world repeats every LCM(8,5,6,3) = 120 columns, so wrapping the camera by
 * that many tiles keeps the procedural structures seamless while bounding the
 * integer. (Every structure period below divides 120.) */
#define WORLD_PERIOD_COLS 120
#define WORLD_PERIOD_PX (WORLD_PERIOD_COLS * TILE)

/* ---- Palettes (index 0 is transparent for the actors/decorations) ---- */
static const uint16_t s_pal_brick[4] = {RGB(45, 25, 22), RGB(150, 55, 45), RGB(105, 100, 105), RGB(190, 90, 72)};
static const uint16_t s_pal_stone[4] = {RGB(35, 35, 42), RGB(125, 125, 135), RGB(55, 55, 65), RGB(180, 182, 195)};
static const uint16_t s_pal_ground[4] = {RGB(60, 40, 25), RGB(120, 80, 45), RGB(70, 165, 60), RGB(95, 62, 35)};
static const uint16_t s_pal_torch[4] = {0, RGB(120, 72, 35), RGB(240, 140, 30), RGB(255, 232, 95)};
static const uint16_t s_pal_coin[4] = {0, RGB(120, 90, 10), RGB(240, 200, 40), RGB(255, 245, 180)};
static const uint16_t s_pal_heart[4] = {0, RGB(220, 50, 55), RGB(150, 25, 35), RGB(255, 150, 150)};
static const uint16_t s_pal_hero[16] = {
    0, RGB(45, 65, 170), RGB(240, 195, 155), RGB(25, 18, 18),
    RGB(205, 55, 55), RGB(95, 72, 145), RGB(70, 45, 32)};
static const uint16_t s_pal_slime[16] = {0, RGB(90, 205, 95), RGB(45, 130, 55), RGB(15, 15, 15)};

/* ---- Font-colour palettes (slot 0 transparent, 1-3 the ink) ---- */
static const uint16_t s_pal_font_white[4] = {0x0000, 0xFFFF, 0xFFFF, 0xFFFF};
static const uint16_t s_pal_font_green[4] = {0x0000, 0x07E0, 0x07E0, 0x07E0};
static const uint16_t s_pal_font_amber[4] = {0x0000, 0xFD20, 0xFD20, 0xFD20};
static const uint16_t s_pal_font_cyan[4] = {0x0000, 0x07FF, 0x07FF, 0x07FF};

/* ---- Tile art: one character per pixel, indices into the tile's palette ---- */
static const char s_art_brick[] = /* opaque: running-bond brick */
    "3333333333333332"
    "1111111111111112"
    "1111111111111112"
    "1111111111111112"
    "1111111111111112"
    "1111111111111112"
    "1111111111111112"
    "2222222222222222"
    "3333333233333333"
    "1111111211111111"
    "1111111211111111"
    "1111111211111111"
    "1111111211111111"
    "1111111211111111"
    "1111111211111111"
    "2222222222222222";
static const char s_art_stone[] = /* opaque: beveled stone block */
    "2222222222222222"
    "2333333333333332"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2311111111111132"
    "2111111111111112"
    "2222222222222222";
static const char s_art_ground[] = /* opaque: dirt with a grass top */
    "2222222222222222"
    "2122212221222122"
    "1111111111111111"
    "1113111111311111"
    "1111111111111111"
    "1131111113111111"
    "1111111111111111"
    "1111311111111131"
    "1111111111111111"
    "1311111111311111"
    "1111111111111111"
    "1111111311111111"
    "3111111111111311"
    "1111111111111111"
    "1111131111111111"
    "1111111111111111";
static const char s_art_torch[] = /* transparent 2bpp: wall torch with flame */
    "0000000000000000"
    "0000000330000000"
    "0000003223000000"
    "0000003223000000"
    "0000000330000000"
    "0000000110000000"
    "0000000110000000"
    "0000000110000000"
    "0000000110000000"
    "0000000110000000"
    "0000000110000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000";
static const char s_art_coin[] = /* transparent 2bpp: spinning coin / pickup */
    "0000011111100000"
    "0001133333311000"
    "0013322222233100"
    "0133222222223310"
    "0132222332222310"
    "1322223333222231"
    "1322233333322231"
    "1322233333322231"
    "1322233333322231"
    "1322223333222231"
    "0132222332222310"
    "0133222222223310"
    "0013322222233100"
    "0001133333311000"
    "0000011111100000"
    "0000000000000000";
static const char s_art_heart[] = /* transparent 2bpp: HUD heart */
    "0000000000000000"
    "0011100011100000"
    "0122210122210000"
    "1233321233321000"
    "1222222222221000"
    "1222222222221000"
    "0122222222210000"
    "0012222222100000"
    "0001222221000000"
    "0000122210000000"
    "0000012100000000"
    "0000001000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000";
static const char s_art_hero[] = /* transparent 4bpp: the player */
    "0000011111100000"
    "0000111111110000"
    "0001111111111000"
    "0001222222221000"
    "0001232232321000"
    "0001222222221000"
    "0000122222210000"
    "0000444444440000"
    "0004444444444000"
    "0004444444444000"
    "0004444444444000"
    "0000455555540000"
    "0000555005550000"
    "0000555005550000"
    "0000666006660000"
    "0006660000666000";
static const char s_art_slime[] = /* transparent 4bpp: enemy */
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000011111100000"
    "0000111111110000"
    "0001113113111000"
    "0011111111111100"
    "0111111111111110"
    "0111111111111110"
    "0111111111111110"
    "0111111111111110"
    "0011111111111100"
    "0001111111111000"
    "0021021021021200"
    "0000000000000000";

/* Packed tiles (built once from the art above). 2bpp = 64 B, 4bpp = 128 B. The
 * game's own RAM (.bss in GAME_RAM) holds these — no special section is needed,
 * unlike the old in-console harness which had to tag them out of CONSOLE_RAM. */
static uint8_t s_brick[64];
static uint8_t s_stone[64];
static uint8_t s_ground[64];
static uint8_t s_torch[64];
static uint8_t s_coin[64];
static uint8_t s_heart[64];
static uint8_t s_hero[128];
static uint8_t s_slime[128];

/* Scene arrays, borrowed by the renderer; sized for one heavy visible window.
 * (The renderer caps each layer at 350; these stay comfortably under it.) */
static Sprite s_bg[VIS_COLS * ROWS]; /* 315 brick tiles — full grid */
static Sprite s_fg[256];             /* terrain, towers, platforms, props, enemies */
static Sprite s_ui[128];             /* HUD icons + FPS counters + font showcase */

/* Pool for scaled-glyph pixel data. A draw_text_scaled call advances a write
 * pointer through it; each glyph gets a fresh slot so every sprite's .pixels
 * pointer stays valid until rendererRender() returns. */
#define SCALED_POOL_SIZE 1024U
static uint8_t s_scaled_pool[SCALED_POOL_SIZE];

/* World/animation state. Almost no game logic: a free-running frame counter for
 * cheap bobbing, and a camera that slides right forever (wrapped to stay bounded). */
static uint32_t s_tick;
static int s_camera_x;

/* ---- FPS measurement (min / avg / max over a ~1 s window) -------------------
 * Frames are timed with getSysTime() (1 ms tick): coarse at ~70 FPS, but plenty
 * for an on-screen overlay. The instantaneous min/max naturally show the jitter;
 * the average is steady. The latched values update once per window. */
static uint16_t s_fps_min, s_fps_avg, s_fps_max; /* displayed */
static uint32_t s_win_start_ms;                  /* current window start */
static uint32_t s_win_frames;                    /* frames counted this window */
static uint32_t s_last_frame_ms;                 /* timestamp of the previous frame */
static uint16_t s_win_fps_lo, s_win_fps_hi;      /* instantaneous extremes this window */

static void packTile(uint8_t *out, const char *art, bool is_4bpp)
{
    uint16_t stride = is_4bpp ? 8U : 4U;
    memset(out, 0, stride * TILE);
    for (uint16_t y = 0U; y < TILE; y++)
    {
        for (uint16_t x = 0U; x < TILE; x++)
        {
            char ch = art[y * TILE + x];
            uint8_t idx = (ch <= '9') ? (uint8_t)(ch - '0') : (uint8_t)(ch - 'a' + 10);
            if (is_4bpp)
            {
                out[y * stride + (x >> 1)] |= (uint8_t)(idx << (4U - (x & 1U) * 4U));
            }
            else
            {
                out[y * stride + (x >> 2)] |= (uint8_t)(idx << (6U - (x & 3U) * 2U));
            }
        }
    }
}

static Sprite tile(int16_t x, int16_t y, uint8_t z, uint8_t flags,
                   const uint8_t *pixels, const uint16_t *palette)
{
    return (Sprite){.x = x, .y = y, .w = TILE, .h = TILE, .z = z, .flags = flags, .pixels = pixels, .palette = palette};
}

/* A 0..amp..0 triangle wave from the frame counter, for cheap bobbing. */
static int bob(uint32_t phase, uint32_t period, int amp)
{
    uint32_t p = ((s_tick / 4U) + phase) % (period * 2U);
    int v = (int)(p < period ? p : period * 2U - p); /* 0..period */
    return (int)((long)v * amp / (long)period);      /* 0..amp */
}

/* ---- Text helpers ----------------------------------------------------------
 * Unlike the old harness (which held a console-internal Font*), a game names a
 * font by its FontSize and reaches the glyph data through the font syscalls. */

#define MAX_UI_TEXT (sizeof(s_ui) / sizeof(s_ui[0]))

/* Render a C string into consecutive UI sprites. Returns the next free index in
 * the caller's sprite array. The glyph pixels point straight at console flash
 * (returned by fontGet), so no pool is needed. */
static uint16_t draw_text(Sprite *ui, uint16_t idx, FontSize size,
                          int16_t x, int16_t y, uint8_t z,
                          const uint16_t *palette, const char *text)
{
    uint16_t glyphW = fontGlyphW(size);
    uint16_t glyphH = fontGlyphH(size);

    for (const char *scan = text; *scan != '\0' && idx < MAX_UI_TEXT; scan++)
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

/* Scale a string into a caller-owned buffer pool and emit sprites. *writeCursor
 * advances by one glyph slot per character; the caller seeds it to the start of a
 * pool large enough for the whole string and must keep the pool alive until
 * rendererRender() returns. */
static uint16_t draw_text_scaled(Sprite *ui, uint16_t idx, FontSize size,
                                 int16_t x, int16_t y, uint8_t z,
                                 uint8_t factor, const uint16_t *palette,
                                 const char *text,
                                 uint8_t **writeCursor, uint16_t poolRemain)
{
    uint8_t scaledW = (uint8_t)(fontGlyphW(size) * factor);
    uint8_t scaledH = (uint8_t)(fontGlyphH(size) * factor);
    uint16_t slotBytes = fontSize(size, factor);

    for (const char *scan = text; *scan != '\0' && idx < MAX_UI_TEXT; scan++)
    {
        uint8_t ascii = (uint8_t)*scan;
        if (ascii >= 0x20U && ascii <= 0x7EU && poolRemain >= slotBytes)
        {
            fontScale(ascii, size, factor, *writeCursor);
            ui[idx] = (Sprite){.x = x, .y = y, .w = scaledW, .h = scaledH, .z = z,
                               .flags = 0U, .pixels = *writeCursor, .palette = palette};
            idx++;
            *writeCursor = *writeCursor + slotBytes;
            poolRemain = (uint16_t)(poolRemain - slotBytes);
        }
        x = (int16_t)(x + scaledW + factor);
    }
    return idx;
}

/* Sample one frame's duration and, once per ~1 s window, latch min/avg/max FPS. */
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
    s_win_frames++;

    const uint32_t elapsed = now - s_win_start_ms;
    if (elapsed >= 1000U)
    {
        s_fps_avg = (uint16_t)(s_win_frames * 1000U / elapsed);
        s_fps_min = s_win_fps_lo;
        s_fps_max = s_win_fps_hi;
        gameLog("renderer test: fps min=%u avg=%u max=%u",
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

/* LAYER_BG: a full screen-filling brick grid behind everything. */
static uint16_t buildBackground(uint16_t first_col)
{
    uint16_t nb = 0U;
    for (uint16_t vc = 0U; vc < VIS_COLS; vc++)
    {
        int16_t sx = (int16_t)((int)(first_col + vc) * TILE - s_camera_x);
        for (uint8_t row = 0U; row < ROWS; row++)
        {
            s_bg[nb++] = tile(sx, (int16_t)(row * TILE), 0U, SPRITE_OPAQUE, s_brick, s_pal_brick);
        }
    }
    return nb;
}

/* LAYER_FG: dense midground over the bricks — terrain, towers, platforms, torches,
 * floating coins and patrolling slimes, plus the bobbing hero centred on screen.
 * Heavy on overdraw on purpose (opaque tiles stacked over the full BG grid). */
static uint16_t buildForeground(uint16_t first_col)
{
    uint16_t nf = 0U;

    for (uint16_t vc = 0U; vc < VIS_COLS && nf < (uint16_t)(sizeof(s_fg) / sizeof(s_fg[0]) - 1U); vc++)
    {
        uint16_t col = first_col + vc;
        int16_t sx = (int16_t)((int)col * TILE - s_camera_x);

        for (uint8_t r = terrainTop(col); r < ROWS; r++) /* hilly ground column */
        {
            s_fg[nf++] = tile(sx, (int16_t)(r * TILE), 1U, SPRITE_OPAQUE, s_ground, s_pal_ground);
        }
        if (col % 8U == 0U || col % 8U == 1U) /* a stone tower every 8 columns */
        {
            for (uint8_t r = 6U; r <= 13U; r++)
            {
                s_fg[nf++] = tile(sx, (int16_t)(r * TILE), 2U, SPRITE_OPAQUE, s_stone, s_pal_stone);
            }
            if (col % 8U == 0U) /* torch on the tower */
            {
                s_fg[nf++] = tile(sx, (int16_t)(7 * TILE), 10U, 0U, s_torch, s_pal_torch);
            }
        }
        if (col % 5U == 2U) /* floating stone platform */
        {
            s_fg[nf++] = tile(sx, (int16_t)(8 * TILE), 3U, SPRITE_OPAQUE, s_stone, s_pal_stone);
        }
        if (col % 3U == 0U) /* bobbing coin */
        {
            int16_t cy = (int16_t)(5 * TILE + bob(col, 6U, TILE));
            s_fg[nf++] = tile(sx, cy, 12U, 0U, s_coin, s_pal_coin);
        }
        if (col % 6U == 4U) /* a slime patrolling the terrain top */
        {
            int16_t gy = (int16_t)((terrainTop(col) - 1U) * TILE);
            s_fg[nf++] = tile(sx, gy, 20U, SPRITE_IS_FMT_4BPP, s_slime, s_pal_slime);
        }
    }

    /* The hero sits centred and just bobs — the world scrolls past him. */
    s_fg[nf++] = tile((int16_t)((RENDERER_WIDTH - TILE) / 2),
                      (int16_t)(10 * TILE - bob(0U, 4U, 4)),
                      30U, SPRITE_IS_FMT_4BPP, s_hero, s_pal_hero);
    return nf;
}

/* LAYER_UI: a busy HUD over everything — a row of hearts and coins, the three FPS
 * counters the test exists to show, and a font showcase that doubles as load. */
static uint16_t buildOverlay(void)
{
    uint16_t nu = 0U;
    char line[16];
    uint8_t *poolCursor = s_scaled_pool;

    for (uint8_t i = 0U; i < 8U; i++) /* hearts top-right */
    {
        s_ui[nu++] = tile((int16_t)(196 + i * 15), 2, 0U, 0U, s_heart, s_pal_heart);
    }
    for (uint8_t i = 0U; i < 8U; i++) /* coins below the hearts */
    {
        s_ui[nu++] = tile((int16_t)(196 + i * 15), 20, 0U, 0U, s_coin, s_pal_coin);
    }

    snprintf(line, sizeof(line), "MIN %u", (unsigned)s_fps_min);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 4, 0U, s_pal_font_amber, line);
    snprintf(line, sizeof(line), "AVG %u", (unsigned)s_fps_avg);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 14, 0U, s_pal_font_green, line);
    snprintf(line, sizeof(line), "MAX %u", (unsigned)s_fps_max);
    nu = draw_text(s_ui, nu, FONT_8x8, 4, 24, 0U, s_pal_font_cyan, line);

    nu = draw_text_scaled(s_ui, nu, FONT_3x5, 96, 104, 0U, 3U,
                          s_pal_font_white, "RENDERER TEST",
                          &poolCursor, SCALED_POOL_SIZE);

    nu = draw_text(s_ui, nu, FONT_3x5, 96, 124, 0U, s_pal_font_white, "The quick brown fox jumps");
    nu = draw_text(s_ui, nu, FONT_5x5, 4, 226, 0U, s_pal_font_cyan, "SB2 EXIT");
    return nu;
}

/* Rebuild and submit the whole visible window. Structures are decided procedurally
 * from the absolute column, so the level repeats with variety forever — no map. */
static void buildVisibleScene(void)
{
    uint16_t first_col = (uint16_t)(s_camera_x / TILE);

    rendererSubmitLayer(LAYER_BG, s_bg, buildBackground(first_col));
    rendererSubmitLayer(LAYER_FG, s_fg, buildForeground(first_col));
    rendererSubmitLayer(LAYER_UI, s_ui, buildOverlay());
}

/*
 * Game lifecycle. The OS runs the C-runtime bootstrap, then init once, then
 * update/render every frame back to back (uncapped). This benchmark never paces
 * itself — running flat out is the whole point.
 */

static void gameInit(void)
{
    packTile(s_brick, s_art_brick, false);
    packTile(s_stone, s_art_stone, false);
    packTile(s_ground, s_art_ground, false);
    packTile(s_torch, s_art_torch, false);
    packTile(s_coin, s_art_coin, false);
    packTile(s_heart, s_art_heart, false);
    packTile(s_hero, s_art_hero, true);
    packTile(s_slime, s_art_slime, true);

    rendererSetBackground(RGB(20, 18, 30)); /* shows only where nothing draws */

    s_win_start_ms = getSysTime();
    s_win_fps_lo = 0xFFFFU;
    s_win_fps_hi = 0U;
    gameLog("renderer test: endless auto-scroller, heavy on all 3 layers; SB2 to exit");
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
