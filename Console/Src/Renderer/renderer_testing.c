#include "renderer_testing.h"
#include <stm32f407xx.h>
#include "renderer.h"
#include "sysclock.h"
#include "joystick.h"
#include "string.h"
#include "stdio.h"

#ifdef RENDERER_TESTING
/* A scrolling side-view castle level used to measure the renderer under a realistic,
 * moving load. A brick wall scrolls behind the hero; stone towers, floating
 * platforms, torches and slimes stream past; the hero is driven by the right stick
 * and the camera follows. Every frame rebuilds only the visible column window and
 * re-submits it — exactly what a real tile game does. The opaque tiles (brick, stone,
 * ground) keep SPRITE_OPAQUE so the decoded-tile cache applies: scrolling moves their
 * screen x but the cache is keyed by graphic + palette, so it survives the motion.
 *
 * Game-owned data lives in GAME_RAM (see console.ld) so it doesn't count against the
 * console's CONSOLE_RAM budget, mirroring how a loaded game owns its sprites. */
#define GAME_RAM_DATA __attribute__((section(".game_ram")))

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define TILE 16
#define COLS (RENDERER_WIDTH / TILE)  /* 20 on-screen columns */
#define ROWS (RENDERER_HEIGHT / TILE) /* 15 rows */
#define VIS_COLS (COLS + 1)           /* one extra column for the partial edge tile */
#define HERO_SPEED 3

/* ---- Palettes (index 0 is transparent for the actors/decorations) ---- */
static const uint16_t s_pal_brick[4] = {RGB(45, 25, 22), RGB(150, 55, 45), RGB(105, 100, 105), RGB(190, 90, 72)};
static const uint16_t s_pal_stone[4] = {RGB(35, 35, 42), RGB(125, 125, 135), RGB(55, 55, 65), RGB(180, 182, 195)};
static const uint16_t s_pal_ground[4] = {RGB(60, 40, 25), RGB(120, 80, 45), RGB(70, 165, 60), RGB(95, 62, 35)};
static const uint16_t s_pal_torch[4] = {0, RGB(120, 72, 35), RGB(240, 140, 30), RGB(255, 232, 95)};
static const uint16_t s_pal_heart[4] = {0, RGB(220, 50, 55), RGB(150, 25, 35), RGB(255, 150, 150)};
static const uint16_t s_pal_hero[16] = {
    0, RGB(45, 65, 170), RGB(240, 195, 155), RGB(25, 18, 18),
    RGB(205, 55, 55), RGB(95, 72, 145), RGB(70, 45, 32)};
static const uint16_t s_pal_slime[16] = {0, RGB(90, 205, 95), RGB(45, 130, 55), RGB(15, 15, 15)};

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

/* Packed tiles (built once from the art above). 2bpp = 64 B, 4bpp = 128 B. */
static uint8_t s_brick[64] GAME_RAM_DATA;
static uint8_t s_stone[64] GAME_RAM_DATA;
static uint8_t s_ground[64] GAME_RAM_DATA;
static uint8_t s_torch[64] GAME_RAM_DATA;
static uint8_t s_heart[64] GAME_RAM_DATA;
static uint8_t s_hero[128] GAME_RAM_DATA;
static uint8_t s_slime[128] GAME_RAM_DATA;

/* Scene arrays, borrowed by the renderer; sized for one visible window. */
static Sprite s_bg[VIS_COLS * ROWS] GAME_RAM_DATA; /* 315 brick tiles */
static Sprite s_fg[128] GAME_RAM_DATA;             /* ground, towers, props, hero */
static Sprite s_ui[8] GAME_RAM_DATA;               /* HUD hearts */

/* World/camera state (world coords are in pixels; the level scrolls endlessly). */
static int s_hero_world_x = 152;
static int16_t s_hero_y = 12 * TILE;
static int s_camera_x;

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

/* Move the hero with the right stick and slide the camera so it follows him. */
static void updateHero(void)
{
    JoystickAxisState ax = joystickGetRAnalogX();
    JoystickAxisState ay = joystickGetRAnalogY();
    if (ax == JoystickAxisStatePositive)
    {
        s_hero_world_x += HERO_SPEED;
    }
    if (ax == JoystickAxisStateNegative)
    {
        s_hero_world_x -= HERO_SPEED;
    }
    if (ay == JoystickAxisStatePositive)
    {
        s_hero_y += HERO_SPEED;
    }
    if (ay == JoystickAxisStateNegative)
    {
        s_hero_y -= HERO_SPEED;
    }
    if (s_hero_world_x < 0)
    {
        s_hero_world_x = 0;
    }
    if (s_hero_world_x > 30000)
    {
        s_hero_world_x = 30000; /* keep screen coords inside int16 */
    }
    if (s_hero_y < 0)
    {
        s_hero_y = 0;
    }
    if (s_hero_y > (int16_t)(RENDERER_HEIGHT - TILE))
    {
        s_hero_y = (int16_t)(RENDERER_HEIGHT - TILE);
    }

    /* Centre the hero; clamp the camera at the left edge so the world never scrolls
     * past its start. There is no right edge — the level is procedurally endless. */
    s_camera_x = s_hero_world_x - (RENDERER_WIDTH - TILE) / 2;
    if (s_camera_x < 0)
    {
        s_camera_x = 0;
    }
}

/* Rebuild only the columns currently on screen, placed at screen x = world - camera,
 * and submit them. Structures are decided procedurally from the absolute column, so
 * the level repeats with variety forever without storing a map. */
static void buildVisibleScene(void)
{
    uint16_t first_col = (uint16_t)(s_camera_x / TILE);
    uint16_t nb = 0U, nf = 0U, nu = 0U;

    for (uint16_t vc = 0U; vc < VIS_COLS; vc++)
    {
        uint16_t col = first_col + vc;
        int16_t sx = (int16_t)((int)col * TILE - s_camera_x); /* may be negative; clipped */

        for (uint8_t row = 0U; row < ROWS; row++) /* brick wall behind everything */
        {
            s_bg[nb++] = tile(sx, (int16_t)(row * TILE), 0U, SPRITE_OPAQUE, s_brick, s_pal_brick);
        }
        s_fg[nf++] = tile(sx, (int16_t)(14 * TILE), 1U, SPRITE_OPAQUE, s_ground, s_pal_ground);

        if (col % 14U == 5U || col % 14U == 6U) /* a stone tower every 14 columns */
        {
            for (uint8_t r = 7U; r <= 13U; r++)
            {
                s_fg[nf++] = tile(sx, (int16_t)(r * TILE), 1U, SPRITE_OPAQUE, s_stone, s_pal_stone);
            }
        }
        if (col % 14U == 5U) /* torch on the tower */
        {
            s_fg[nf++] = tile(sx, (int16_t)(9 * TILE), 10U, 0U, s_torch, s_pal_torch);
        }
        if (col % 11U == 4U || col % 11U == 5U || col % 11U == 6U) /* floating platform */
        {
            s_fg[nf++] = tile(sx, (int16_t)(10 * TILE), 2U, SPRITE_OPAQUE, s_stone, s_pal_stone);
        }
        if (col % 6U == 3U) /* a slime patrolling the ground */
        {
            s_fg[nf++] = tile(sx, (int16_t)(13 * TILE), 20U, SPRITE_IS_FMT_4BPP, s_slime, s_pal_slime);
        }
    }

    s_fg[nf++] = tile((int16_t)(s_hero_world_x - s_camera_x), s_hero_y, 30U,
                      SPRITE_IS_FMT_4BPP, s_hero, s_pal_hero);

    rendererSubmitLayer(LAYER_BG, s_bg, nb);
    rendererSubmitLayer(LAYER_FG, s_fg, nf);

    for (uint8_t i = 0U; i < 3U; i++) /* HUD hearts, fixed to the screen */
    {
        s_ui[nu++] = tile((int16_t)(4 + i * 18), 4, 0U, 0U, s_heart, s_pal_heart);
    }
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
}

void rendererTestingInit(void)
{
    packTile(s_brick, s_art_brick, false);
    packTile(s_stone, s_art_stone, false);
    packTile(s_ground, s_art_ground, false);
    packTile(s_torch, s_art_torch, false);
    packTile(s_heart, s_art_heart, false);
    packTile(s_hero, s_art_hero, true);
    packTile(s_slime, s_art_slime, true);

    rendererSetBackground(RGB(20, 18, 30)); /* shows only where nothing draws */
    printf("Scrolling castle: move the hero with the right stick; the world follows.\n");
}

void rendererTestingUpdate(void)
{
    updateHero();
    buildVisibleScene();
}

void rendererTestingRender(void)
{
    /* DWT->CYCCNT is enabled in swoInit(); 168 cycles == 1us at 168MHz SYSCLK. */
    static uint32_t last, fc, cmin = 0xFFFFFFFFU, cmax, csum;
    static bool started;
    if (!started)
    {
        last = getSysTime();
        started = true;
    }

    uint32_t c0 = DWT->CYCCNT;
    rendererRender();
    uint32_t cyc = DWT->CYCCNT - c0;

    if (cyc < cmin)
    {
        cmin = cyc;
    }
    if (cyc > cmax)
    {
        cmax = cyc;
    }
    csum += cyc;
    fc++;
    if (getSysTime() - last >= 2000U)
    {
        uint32_t avg = csum / fc;
        printf("FPS=%lu  render avg=%luus [%lu..%lu]us  cam=%lupx  (%lu cyc)\n",
               (unsigned long)(fc * 1000U / (getSysTime() - last)),
               (unsigned long)(avg / 168U), (unsigned long)(cmin / 168U),
               (unsigned long)(cmax / 168U), (unsigned long)s_camera_x, (unsigned long)avg);
        fc = 0U;
        cmin = 0xFFFFFFFFU;
        cmax = 0U;
        csum = 0U;
        last = getSysTime();
    }
}
#endif