#include "menu_common.h"
#include "font_utils.h"
#include "joystick.h"
#include "sysclock.h"
#include <string.h>

/* ------------------------------------------------------------------ *
 *  Theme palettes, the shared sprite buffer, and the draw/input
 *  helpers extracted from the original single-screen menu so every
 *  screen renders with the same look and the same debounced controls.
 * ------------------------------------------------------------------ */

const uint16_t g_menu_pal_title[4] = {0, MENU_COL_TITLE, MENU_COL_TITLE, MENU_COL_TITLE};
const uint16_t g_menu_pal_accent[4] = {0, MENU_COL_ACCENT, MENU_COL_ACCENT, MENU_COL_ACCENT};
const uint16_t g_menu_pal_item[4] = {0, MENU_COL_ITEM, MENU_COL_ITEM, MENU_COL_ITEM};
const uint16_t g_menu_pal_item_sel[4] = {0, MENU_COL_ITEM_SEL, MENU_COL_ITEM_SEL, MENU_COL_ITEM_SEL};
const uint16_t g_menu_pal_footer[4] = {0, MENU_COL_FOOTER, MENU_COL_FOOTER, MENU_COL_FOOTER};
const uint16_t g_menu_pal_empty[4] = {0, MENU_COL_EMPTY, MENU_COL_EMPTY, MENU_COL_EMPTY};
const uint16_t g_menu_pal_alert[4] = {0, MENU_COL_ALERT, MENU_COL_ALERT, MENU_COL_ALERT};

Sprite g_menu_ui[MENU_MAX_SPRITES];

/* Title geometry. */
#define TITLE_SCALE 2U
#define TITLE_Y 30
#define UNDERLINE_Y 56

/* A 16x4 solid (all index 1, opaque) 2bpp tile, tiled to draw the underline. */
#define BAR_SEG_W 16
#define BAR_SEG_H 4
static uint8_t s_bar_tile[(BAR_SEG_W * BAR_SEG_H * 2) / 8];

/* Scaled-title glyph pool: a 16-char title * 64 B (16x16 @ 2bpp) fits in 1 KB. */
#define TITLE_POOL_SIZE 1024U
static uint8_t s_title_pool[TITLE_POOL_SIZE];

void menuCommonInit(void)
{
    memset(s_bar_tile, 0x55, sizeof(s_bar_tile)); /* 0x55 = four index-1 pixels per byte */
}

void menuResetSurface(void)
{
    rendererSetBackground(MENU_COL_BG);
}

static uint16_t glyphAdvance(FontSize size)
{
    return (uint16_t)(fontGlyphW(size) + 1U);
}

uint16_t menuTextWidth(FontSize size, const char *text)
{
    uint16_t n = (uint16_t)strlen(text);
    return (n == 0U) ? 0U : (uint16_t)(n * glyphAdvance(size) - 1U);
}

uint16_t menuDrawText(uint16_t idx, const Font *font, int16_t x, int16_t y,
                      const uint16_t *palette, const char *text)
{
    const FontSize size = font->size;
    const uint16_t gw = fontGlyphW(size);
    const uint16_t gh = fontGlyphH(size);

    for (const char *scan = text; *scan != '\0' && idx < MENU_MAX_SPRITES; scan++)
    {
        const uint8_t ascii = (uint8_t)*scan;
        if (ascii >= 0x20U && ascii <= 0x7EU)
        {
            const uint8_t *pixels;
            fontGet(ascii, size, &pixels);
            g_menu_ui[idx++] = (Sprite){.x = x, .y = y, .w = gw, .h = gh, .z = 1U,
                                        .flags = 0U, .pixels = pixels, .palette = palette};
        }
        x = (int16_t)(x + glyphAdvance(size));
    }
    return idx;
}

/* Nearest-neighbour scaled text; glyph pixels are baked into the title pool. */
static uint16_t drawTextScaled(uint16_t idx, const Font *font, int16_t x, int16_t y,
                               uint8_t factor, const uint16_t *palette, const char *text)
{
    const FontSize size = font->size;
    const uint8_t sw = (uint8_t)(fontGlyphW(size) * factor);
    const uint8_t sh = (uint8_t)(fontGlyphH(size) * factor);
    const uint16_t slot = fontSize(size, factor);
    uint8_t *cursor = s_title_pool;
    uint16_t remaining = TITLE_POOL_SIZE;

    for (const char *scan = text; *scan != '\0' && idx < MENU_MAX_SPRITES; scan++)
    {
        const uint8_t ascii = (uint8_t)*scan;
        if (ascii >= 0x20U && ascii <= 0x7EU && remaining >= slot)
        {
            fontScale(ascii, size, factor, cursor);
            g_menu_ui[idx++] = (Sprite){.x = x, .y = y, .w = sw, .h = sh, .z = 1U,
                                        .flags = 0U, .pixels = cursor, .palette = palette};
            cursor += slot;
            remaining = (uint16_t)(remaining - slot);
        }
        x = (int16_t)(x + sw + factor);
    }
    return idx;
}

/* A horizontal bar of `width` px, starting at x, on row y, in `palette`'s ink. */
uint16_t menuDrawBar(uint16_t idx, int16_t x, int16_t y, uint16_t width, const uint16_t *palette)
{
    for (int16_t sx = x; sx < (int16_t)(x + width) && idx < MENU_MAX_SPRITES; sx += BAR_SEG_W)
    {
        g_menu_ui[idx++] = (Sprite){.x = sx, .y = y, .w = BAR_SEG_W, .h = BAR_SEG_H, .z = 0U,
                                    .flags = SPRITE_OPAQUE, .pixels = s_bar_tile, .palette = palette};
    }
    return idx;
}

uint16_t menuDrawTitle(uint16_t idx, const char *text)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const uint16_t glyph_step = (uint16_t)(fontGlyphW(font8x8.size) * TITLE_SCALE + TITLE_SCALE);
    const uint16_t title_w = (uint16_t)(strlen(text) * glyph_step);
    const int16_t title_x = (int16_t)((screen_w - (int16_t)title_w) / 2);

    idx = drawTextScaled(idx, &font8x8, title_x, TITLE_Y, TITLE_SCALE, g_menu_pal_title, text);
    idx = menuDrawBar(idx, title_x, UNDERLINE_Y, title_w, g_menu_pal_accent);
    return idx;
}

uint16_t menuDrawFooter(uint16_t idx, const char *text)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const int16_t fx = (int16_t)((screen_w - (int16_t)menuTextWidth(font5x5.size, text)) / 2);
    return menuDrawText(idx, &font5x5, fx, MENU_FOOTER_Y, g_menu_pal_footer, text);
}

MenuNav menuPollNav(void)
{
    static uint32_t last_action = 0U;
    const uint32_t DEBOUNCE_MS = 220U;
    const uint32_t now = getSysTime();
    MenuNav nav = {0};

    if (now <= last_action + DEBOUNCE_MS)
    {
        return nav; /* still inside the lockout window */
    }

    /* Up/down comes from either d-pad or the right analog stick (Positive Y = up). */
    const bool up = joystickGetLBtnUp() || joystickGetRBtnUp() ||
                    (joystickGetRAnalogY() == JoystickAxisStatePositive);
    const bool down = joystickGetLBtnDown() || joystickGetRBtnDown() ||
                      (joystickGetRAnalogY() == JoystickAxisStateNegative);
    const bool enter = joystickGetSpecialBtn1();
    const bool back = joystickGetSpecialBtn2();

    /* Latch a single edge per window; vertical motion takes precedence. */
    if (up)
    {
        nav.up = true;
    }
    else if (down)
    {
        nav.down = true;
    }
    else if (enter)
    {
        nav.enter = true;
    }
    else if (back)
    {
        nav.back = true;
    }

    if (nav.up || nav.down || nav.enter || nav.back)
    {
        last_action = now;
    }
    return nav;
}
