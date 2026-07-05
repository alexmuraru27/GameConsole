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

/* Text is drawn straight through the renderer's console-side text path (glyph
 * expansion + scaled-glyph caching live there); only the bars below still build
 * sprites into g_menu_ui. Menu text sits at z=1 so it composites over the z=0 bars
 * on LAYER_UI. `idx` is returned unchanged — no g_menu_ui slot is consumed. The
 * single-tint colour is palette index 1 (the theme palettes are {0, c, c, c}). */
uint16_t menuDrawText(uint16_t idx, const Font *font, int16_t x, int16_t y,
                      const uint16_t *palette, const char *text)
{
    rendererDrawText(LAYER_UI, x, y, 1U, font->size, 1U, palette[1], text);
    return idx;
}

static uint16_t drawTextScaled(uint16_t idx, const Font *font, int16_t x, int16_t y,
                               uint8_t factor, const uint16_t *palette, const char *text)
{
    rendererDrawText(LAYER_UI, x, y, 1U, font->size, factor, palette[1], text);
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

uint16_t menuGaugeWidth(uint8_t total)
{
    if (total == 0U)
    {
        return 0U;
    }
    return (uint16_t)((int)total * (MENU_GAUGE_CELL_W + MENU_GAUGE_CELL_GAP) - MENU_GAUGE_CELL_GAP);
}

uint16_t menuDrawGauge(uint16_t idx, int16_t x, int16_t y, uint8_t total, uint8_t filled)
{
    for (uint8_t i = 0U; i < total && idx < MENU_MAX_SPRITES; i++)
    {
        const int16_t cx = (int16_t)(x + (int)i * (MENU_GAUGE_CELL_W + MENU_GAUGE_CELL_GAP));
        const uint16_t *pal = (i < filled) ? g_menu_pal_accent : g_menu_pal_footer;
        g_menu_ui[idx++] = (Sprite){.x = cx, .y = y, .w = MENU_GAUGE_CELL_W, .h = BAR_SEG_H, .z = 0U,
                                    .flags = SPRITE_OPAQUE, .pixels = s_bar_tile, .palette = pal};
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

/* Right-stick deflection (of +/-512) that counts as a directional press. */
#define NAV_STICK_THRESHOLD 256
/* Typematic auto-repeat for up/down: wait this long after the initial press, then
 * repeat at a steady rate. Enter/back never repeat (edge-only). */
#define NAV_REPEAT_DELAY_MS 350U
#define NAV_REPEAT_RATE_MS 110U

MenuNav menuPollNav(void)
{
    static bool s_up_stick_prev = false;
    static bool s_down_stick_prev = false;
    static bool s_left_stick_prev = false;
    static bool s_right_stick_prev = false;
    static uint32_t s_repeat_at_v = 0U; /* up/down repeat deadline */
    static uint32_t s_repeat_at_h = 0U; /* left/right repeat deadline */
    const uint32_t now = getSysTime();

    joystickPollFrame();
    InputState in;
    joystickGetState(&in);

    /* Directional intent (held) for auto-repeat: either d-pad or the right stick. */
    const bool up_stick = in.right_y > NAV_STICK_THRESHOLD;
    const bool down_stick = in.right_y < -NAV_STICK_THRESHOLD;
    const bool left_stick = in.right_x < -NAV_STICK_THRESHOLD;
    const bool right_stick = in.right_x > NAV_STICK_THRESHOLD;
    const bool up_held = in.l_up.held || in.r_up.held || up_stick;
    const bool down_held = in.l_down.held || in.r_down.held || down_stick;
    const bool left_held = in.l_left.held || in.r_left.held || left_stick;
    const bool right_held = in.l_right.held || in.r_right.held || right_stick;

    /* Fresh press edges. Button edges come from the per-button pressed flags, which
     * are latched every frame (including while a game runs), so a button still held
     * when we return to a menu does not re-fire. The stick self-centers on release,
     * so a simple local threshold-crossing is enough and carries no stale press. */
    bool up = in.l_up.pressed || in.r_up.pressed || (up_stick && !s_up_stick_prev);
    bool down = in.l_down.pressed || in.r_down.pressed || (down_stick && !s_down_stick_prev);
    bool left = in.l_left.pressed || in.r_left.pressed || (left_stick && !s_left_stick_prev);
    bool right = in.l_right.pressed || in.r_right.pressed || (right_stick && !s_right_stick_prev);
    const bool enter = in.special1.pressed;
    const bool back = in.special2.pressed;
    s_up_stick_prev = up_stick;
    s_down_stick_prev = down_stick;
    s_left_stick_prev = left_stick;
    s_right_stick_prev = right_stick;

    /* Up/down and left/right auto-repeat while held (separate deadlines so one axis'
     * repeat never resets the other); enter/back stay edge-only. */
    if (up || down)
    {
        s_repeat_at_v = now + NAV_REPEAT_DELAY_MS;
    }
    else if ((up_held || down_held) && (int32_t)(now - s_repeat_at_v) >= 0)
    {
        up = up_held;
        down = down_held;
        s_repeat_at_v = now + NAV_REPEAT_RATE_MS;
    }

    if (left || right)
    {
        s_repeat_at_h = now + NAV_REPEAT_DELAY_MS;
    }
    else if ((left_held || right_held) && (int32_t)(now - s_repeat_at_h) >= 0)
    {
        left = left_held;
        right = right_held;
        s_repeat_at_h = now + NAV_REPEAT_RATE_MS;
    }

    /* One action per poll. Vertical motion and enter/back keep the exact precedence
     * they had before left/right existed; the slider's left/right sit last, so they
     * only fire on a frame with no up/down/enter/back — screens that ignore them are
     * unaffected. */
    MenuNav nav = {0};
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
    else if (left)
    {
        nav.left = true;
    }
    else if (right)
    {
        nav.right = true;
    }
    return nav;
}
