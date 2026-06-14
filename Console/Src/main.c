#include <stm32f407xx.h>
#include "sysclock.h"
#include "renderer.h"
#include "game_console.h"
#include "stddef.h"
#include "string.h"
#include "main_menu.h"
#include "stdio.h"
#include "buzzer.h"
#include "joystick.h"

/* Game-owned data lives in GAME_RAM (see console.ld's .game_ram section), so it
 * doesn't count against the console library's CONSOLE_RAM budget — this mirrors
 * the real split where a loaded game owns its sprites and the console only
 * borrows pointers to them. (Test harness only; not production-safe.) */
#define GAME_RAM_DATA __attribute__((section(".game_ram")))

/* ---- Palettes ---- */
static const uint16_t s_pal_bg[4]    = {0, 0xF800, 0x001F, 0};
static const uint16_t s_pal_fg[4]    = {0, 0x07E0, 0, 0};
static const uint16_t s_pal_cursor[4]= {0, 0xFFE0, 0, 0};
static const uint16_t s_pal_text[4]  = {0, 0xFFFF, 0, 0};
static const uint16_t s_pal_panel[4] = {0, 0x8410, 0, 0};

/* ---- 16x16 2bpp tiles ---- */
#define TILE_PX 16U
#define TILE_BYTES ((TILE_PX * TILE_PX) / 4U)
static uint8_t s_tile_red[TILE_BYTES] GAME_RAM_DATA, s_tile_blue[TILE_BYTES] GAME_RAM_DATA;

static void initTiles(void)
{
    memset(s_tile_red,  0x55, TILE_BYTES);
    memset(s_tile_blue, 0xAA, TILE_BYTES);
}

/* ---- 8x8 cursor ---- */
#define CURSOR_PX 8U
#define CURSOR_BYTES ((CURSOR_PX * CURSOR_PX) / 4U)
#define CURSOR_SPEED 3
static uint8_t s_cursor_px[CURSOR_BYTES] GAME_RAM_DATA;
static int16_t s_cursor_x = (RENDERER_WIDTH/2) - (CURSOR_PX/2);
static int16_t s_cursor_y = (RENDERER_HEIGHT/2) - (CURSOR_PX/2);

static void initCursor(void) { memset(s_cursor_px, 0x55, CURSOR_BYTES); }

static void updateCursor(void)
{
    JoystickAxisState ax = joystickGetRAnalogX();
    JoystickAxisState ay = joystickGetRAnalogY();
    if (ax == JoystickAxisStatePositive)  s_cursor_x += CURSOR_SPEED;
    if (ax == JoystickAxisStateNegative)  s_cursor_x -= CURSOR_SPEED;
    if (ay == JoystickAxisStatePositive)  s_cursor_y += CURSOR_SPEED;
    if (ay == JoystickAxisStateNegative)  s_cursor_y -= CURSOR_SPEED;
    if (s_cursor_x < 0) s_cursor_x = 0;
    if (s_cursor_x > (int16_t)(RENDERER_WIDTH  - CURSOR_PX)) s_cursor_x = RENDERER_WIDTH  - CURSOR_PX;
    if (s_cursor_y < 0) s_cursor_y = 0;
    if (s_cursor_y > (int16_t)(RENDERER_HEIGHT - CURSOR_PX)) s_cursor_y = RENDERER_HEIGHT - CURSOR_PX;
}

/* ---- Letters ---- */
#define LETTER_W 8U
#define LETTER_H 8U
#define LETTER_BYTES ((LETTER_W * LETTER_H) / 4U)
static const uint8_t s_letter_bits[5][LETTER_H] = {
    {0x82,0x82,0x82,0xFE,0x82,0x82,0x82,0x00},
    {0xFE,0x80,0x80,0xFC,0x80,0x80,0xFE,0x00},
    {0x80,0x80,0x80,0x80,0x80,0x80,0xFE,0x00},
    {0x80,0x80,0x80,0x80,0x80,0x80,0xFE,0x00},
    {0x7C,0x82,0x82,0x82,0x82,0x82,0x7C,0x00},
};
static uint8_t s_letter_data[5][LETTER_BYTES] GAME_RAM_DATA;

static void initLetters(void)
{
    for (uint8_t li = 0U; li < 5U; li++)
        for (uint8_t row = 0U; row < LETTER_H; row++)
        {
            uint8_t bits = s_letter_bits[li][row], b0 = 0U, b1 = 0U;
            if (bits & 0x80) b0 |= 0x40; if (bits & 0x40) b0 |= 0x10;
            if (bits & 0x20) b0 |= 0x04; if (bits & 0x10) b0 |= 0x01;
            if (bits & 0x08) b1 |= 0x40; if (bits & 0x04) b1 |= 0x10;
            if (bits & 0x02) b1 |= 0x04; if (bits & 0x01) b1 |= 0x01;
            s_letter_data[li][row*2+0] = b0; s_letter_data[li][row*2+1] = b1;
        }
}

/* ---- Submission ---- */
#define GRID_COLS (RENDERER_WIDTH/TILE_PX)
#define GRID_ROWS (RENDERER_HEIGHT/TILE_PX)
#define FG_COLS 15U
#define FG_ROWS 15U
#define UI_CHARS 100U
#define UI_COLS  (RENDERER_WIDTH/(LETTER_W+1))
static const uint8_t s_hello[] = {0,1,2,2,3,255,0,1,2,2,3,255};
#define HLEN (sizeof(s_hello))

/* Game-owned persistent sprite storage (lives in GAME_RAM). The renderer only
 * borrows pointers into this pool, so it must stay valid until rendererRender()
 * returns — it does: we refill it before submitting, and render reads it right
 * after. Sized to the scene (300 BG + 226 FG + 200 UI = 726) with margin. */
#define SCENE_SPRITE_MAX 768U
static Sprite s_scene[SCENE_SPRITE_MAX] GAME_RAM_DATA;
static uint16_t s_scene_count;

/* Store a sprite in the game pool and hand the renderer a pointer to it. */
static void submitSprite(Layer layer, Sprite sprite)
{
    if (s_scene_count >= SCENE_SPRITE_MAX)
        return;
    s_scene[s_scene_count] = sprite;
    rendererSubmit(layer, &s_scene[s_scene_count]);
    s_scene_count++;
}

static void submitFrame(void)
{
    rendererClear();
    s_scene_count = 0U;

    /* BG: full-screen checkerboard (300 sprites) */
    for (uint8_t r = 0U; r < GRID_ROWS; r++)
        for (uint8_t c = 0U; c < GRID_COLS; c++)
            submitSprite(LAYER_BG, (Sprite){
                .x = (int16_t)(c*TILE_PX), .y = (int16_t)(r*TILE_PX),
                .w = TILE_PX, .h = TILE_PX, .z = 0,
                .flags = SPRITE_OPAQUE, .format = GFX_FMT_2BPP,
                .pixels = ((r^c)&1U) ? s_tile_blue : s_tile_red,
                .palette = s_pal_bg,
            });

    /* FG: ~3/4 screen (225 sprites) */
    for (uint8_t r = 0U; r < FG_ROWS; r++)
        for (uint8_t c = 0U; c < FG_COLS; c++)
            submitSprite(LAYER_FG, (Sprite){
                .x = (int16_t)(c*TILE_PX), .y = (int16_t)(r*TILE_PX),
                .w = TILE_PX, .h = TILE_PX, .z = 0,
                .flags = SPRITE_OPAQUE, .format = GFX_FMT_2BPP,
                .pixels = s_tile_red, .palette = s_pal_fg,
            });

    /* FG: cursor */
    submitSprite(LAYER_FG, (Sprite){
        .x = s_cursor_x, .y = s_cursor_y,
        .w = CURSOR_PX, .h = CURSOR_PX, .z = 10,
        .flags = 0, .format = GFX_FMT_2BPP,
        .pixels = s_cursor_px, .palette = s_pal_cursor,
    });

    /* UI: text (100 chars) */
    for (uint16_t i = 0U; i < UI_CHARS; i++)
    {
        uint8_t ch = s_hello[i % HLEN];
        submitSprite(LAYER_UI, (Sprite){
            .x = (int16_t)((i % UI_COLS) * (LETTER_W+1)),
            .y = (int16_t)((i / UI_COLS) * (LETTER_H+1)),
            .w = LETTER_W, .h = LETTER_H, .z = 0,
            .format = GFX_FMT_2BPP,
            .pixels = (ch < 5U) ? s_letter_data[ch] : s_letter_data[4],
            .palette = s_pal_text,
        });
    }

    /* UI: panel ~1/3 screen (100 sprites) */
    for (uint8_t r = 0U; r < 10U; r++)
        for (uint8_t c = 0U; c < 10U; c++)
            submitSprite(LAYER_UI, (Sprite){
                .x = (int16_t)(160 + c*TILE_PX), .y = (int16_t)(80 + r*TILE_PX),
                .w = TILE_PX, .h = TILE_PX, .z = 0,
                .flags = SPRITE_OPAQUE, .format = GFX_FMT_2BPP,
                .pixels = s_tile_red, .palette = s_pal_panel,
            });
}

void SystemInit(void) { systemClockConfig(); }

int main(void)
{
    buzzerSetMute(true);
    gameConsoleInit();
    mainMenuInit();
    initTiles();
    initCursor();
    initLetters();
    printf("Boot OK\n");

    /* DWT->CYCCNT is enabled in swoInit(); 168 cycles == 1us at 168MHz SYSCLK. */
    uint32_t last = getSysTime(), fc = 0U, cmin = 0xFFFFFFFFU, cmax = 0U, csum = 0U;
    while (1)
    {
        mainMenuUpdate();
        updateCursor();
        submitFrame();
        uint32_t c0 = DWT->CYCCNT;
        rendererRender();
        uint32_t cyc = DWT->CYCCNT - c0;
        if (cyc < cmin) cmin = cyc;
        if (cyc > cmax) cmax = cyc;
        csum += cyc;
        fc++;
        if (getSysTime() - last >= 2000U)
        {
            uint32_t avg = csum / fc;
            printf("FPS=%lu  render avg=%luus [%lu..%lu]us  (%lu cyc avg)\n",
                   (unsigned long)(fc * 1000U / (getSysTime() - last)),
                   (unsigned long)(avg / 168U), (unsigned long)(cmin / 168U),
                   (unsigned long)(cmax / 168U), (unsigned long)avg);
            fc = 0U; cmin = 0xFFFFFFFFU; cmax = 0U; csum = 0U; last = getSysTime();
        }
    }
}
