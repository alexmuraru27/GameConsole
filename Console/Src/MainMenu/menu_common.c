#include "menu_common.h"
#include "font_utils.h"
#include "joystick.h"
#include "buzzer.h"
#include "sysclock.h"
#include "Util/utils.h"
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

bool menuCursorVisible(void)
{
    return ((getSysTime() / MENU_BLINK_MS) & 1U) == 0U;
}

static const uint16_t s_move_notes[] = {NOTE_A5, 24U};

void menuBeepMove(void)
{
    buzzerPlay(0U, false, s_move_notes, 1U);
}

bool menuListStep(MenuListState *state, MenuNav nav, int count, int visible_rows)
{
    bool moved = false;
    if (nav.up && state->selected > 0)
    {
        state->selected--;
        moved = true;
    }
    else if (nav.down && state->selected < count - 1)
    {
        state->selected++;
        moved = true;
    }

    /* Scroll the viewport to keep the selection on screen. */
    if (state->selected < state->top)
    {
        state->top = state->selected;
    }
    else if (state->selected >= state->top + visible_rows)
    {
        state->top = state->selected - visible_rows + 1;
    }
    return moved;
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
 * expansion + scaled-glyph caching live there) and consumes NO g_menu_ui slot,
 * so these helpers take no sprite index — only the bars/gauges below build
 * sprites into g_menu_ui. Menu text sits at z=1 so it composites over the z=0
 * bars on LAYER_UI. The single-tint colour is palette index 1 (the theme
 * palettes are {0, c, c, c}). */
static void menuDrawTextScaled(const Font *font, int16_t x, int16_t y, uint8_t scale,
                               const uint16_t *palette, const char *text)
{
    rendererDrawText(LAYER_UI, x, y, 1U, font->size, scale, palette[1], text);
}

void menuDrawText(const Font *font, int16_t x, int16_t y,
                  const uint16_t *palette, const char *text)
{
    menuDrawTextScaled(font, x, y, 1U, palette, text);
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

int16_t menuCenterX(FontSize size, const char *text)
{
    const int16_t w = (int16_t)rendererGetWidthPixels();
    return (int16_t)((w - (int16_t)menuTextWidth(size, text)) / 2);
}

void menuDrawTextCentered(const Font *font, int16_t y, const uint16_t *palette, const char *text)
{
    menuDrawText(font, menuCenterX(font->size, text), y, palette, text);
}

uint16_t menuDrawProgressBar(uint16_t idx, int16_t x, int16_t y, uint16_t w, uint8_t rows,
                             uint32_t done, uint32_t total)
{
    const uint32_t fill = (total > 0U) ? (uint32_t)((uint64_t)w * done / total) : 0U;
    for (uint8_t i = 0U; i < rows; i++)
    {
        const int16_t ry = (int16_t)(y + (int)i * MENU_BAR_H);
        idx = menuDrawBar(idx, x, ry, w, g_menu_pal_footer); /* dim track */
        if (fill > 0U)
        {
            idx = menuDrawBar(idx, x, ry, (uint16_t)fill, g_menu_pal_accent); /* accent fill */
        }
    }
    return idx;
}

void menuModalInfo(const char *title, const char *line, const uint16_t *palette)
{
    uint16_t n = 0U;
    rendererClear();
    n = menuDrawTitle(n, title);
    menuDrawTextCentered(&font8x8, 110, palette, line);
    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

void menuModalWaitBack(const char *title, const char *line, const uint16_t *palette)
{
    while (true)
    {
        const MenuNav nav = menuPollNav();
        if (nav.back)
        {
            return;
        }
        uint16_t n = 0U;
        rendererClear();
        n = menuDrawTitle(n, title);
        menuDrawTextCentered(&font8x8, 110, palette, line);
        menuDrawFooter("Special Button 2: back");
        rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
        rendererRender();
    }
}

uint16_t menuDrawTitle(uint16_t idx, const char *text)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const uint16_t glyph_step = (uint16_t)(fontGlyphW(font8x8.size) * TITLE_SCALE + TITLE_SCALE);
    const uint16_t title_w = (uint16_t)(strlen(text) * glyph_step);
    const int16_t title_x = (int16_t)((screen_w - (int16_t)title_w) / 2);

    menuDrawTextScaled(&font8x8, title_x, TITLE_Y, TITLE_SCALE, g_menu_pal_title, text);
    return menuDrawBar(idx, title_x, UNDERLINE_Y, title_w, g_menu_pal_accent);
}

void menuDrawFooter(const char *text)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const int16_t fx = (int16_t)((screen_w - (int16_t)menuTextWidth(font5x5.size, text)) / 2);
    menuDrawText(&font5x5, fx, MENU_FOOTER_Y, g_menu_pal_footer, text);
}

/* Right-stick deflection (of +/-512) that counts as a directional press. */
#define NAV_STICK_THRESHOLD 256
/* Typematic auto-repeat for up/down: wait this long after the initial press, then
 * repeat at a steady rate. Enter/back never repeat (edge-only). */
#define NAV_REPEAT_DELAY_MS 350U
#define NAV_REPEAT_RATE_MS 110U

/* Persistent state for menuPollNav(): rising-edge detectors for the analog-stick
 * threshold crossings (the d-pad buttons already arrive edge-detected as .pressed),
 * plus the two typematic auto-repeat deadlines. */
EDGE_DETECTOR_DECLARE(s_up_edge, EDGE_RISING);
EDGE_DETECTOR_DECLARE(s_down_edge, EDGE_RISING);
EDGE_DETECTOR_DECLARE(s_left_edge, EDGE_RISING);
EDGE_DETECTOR_DECLARE(s_right_edge, EDGE_RISING);
static uint32_t s_repeat_at_v = 0U; /* up/down repeat deadline */
static uint32_t s_repeat_at_h = 0U; /* left/right repeat deadline */

MenuNav menuPollNav(void)
{
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
     * so a rising edge on its threshold crossing carries no stale press. The edge
     * detectors are stepped once each here (unconditionally) — not inside the ||
     * below, whose short-circuit could skip the state update. */
    const bool up_edge = edgeUpdate(&s_up_edge, up_stick);
    const bool down_edge = edgeUpdate(&s_down_edge, down_stick);
    const bool left_edge = edgeUpdate(&s_left_edge, left_stick);
    const bool right_edge = edgeUpdate(&s_right_edge, right_stick);
    bool up = in.l_up.pressed || in.r_up.pressed || up_edge;
    bool down = in.l_down.pressed || in.r_down.pressed || down_edge;
    bool left = in.l_left.pressed || in.r_left.pressed || left_edge;
    bool right = in.l_right.pressed || in.r_right.pressed || right_edge;
    const bool enter = in.special1.pressed;
    const bool back = in.special2.pressed;

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
