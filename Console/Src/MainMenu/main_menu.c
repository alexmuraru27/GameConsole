#include "main_menu.h"
#include "renderer.h"
#include "loader.h"
#include "game_loader.h"
#include "joystick.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "font_utils.h"
#include "logger.h"
#include <string.h>

/* ------------------------------------------------------------------ *
 *  Centered-hero game picker: a big title over a centered list of the
 *  SD-card games, slate background with cyan accents. Input and drawing
 *  are split (mainMenuUpdate / mainMenuRender) so main() can drive them
 *  on its own loop.
 * ------------------------------------------------------------------ */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Slate + cyan theme. */
#define COL_BG RGB(16, 20, 28)
#define COL_ACCENT RGB(72, 209, 226)   /* cyan: title underline + cursor */
#define COL_TITLE RGB(236, 244, 248)   /* near-white title */
#define COL_ITEM RGB(112, 132, 156)    /* unselected game */
#define COL_ITEM_SEL RGB(156, 232, 244) /* selected game */
#define COL_FOOTER RGB(84, 102, 124)
#define COL_EMPTY RGB(150, 120, 120)

/* Font palettes (2bpp: slot 0 transparent, 1-3 the ink). */
static const uint16_t s_pal_title[4] = {0, COL_TITLE, COL_TITLE, COL_TITLE};
static const uint16_t s_pal_accent[4] = {0, COL_ACCENT, COL_ACCENT, COL_ACCENT};
static const uint16_t s_pal_item[4] = {0, COL_ITEM, COL_ITEM, COL_ITEM};
static const uint16_t s_pal_item_sel[4] = {0, COL_ITEM_SEL, COL_ITEM_SEL, COL_ITEM_SEL};
static const uint16_t s_pal_footer[4] = {0, COL_FOOTER, COL_FOOTER, COL_FOOTER};
static const uint16_t s_pal_empty[4] = {0, COL_EMPTY, COL_EMPTY, COL_EMPTY};

#define MENU_MAX_GAMES 32U
#define MENU_NAME_CHARS 20U /* displayed game-name cap (excludes the .bin) */
#define MENU_VISIBLE_ROWS 6U
#define MENU_ROW_H 20

#define TITLE_TEXT "GAME CONSOLE"
#define TITLE_SCALE 2U
#define TITLE_Y 30
#define UNDERLINE_Y 56
#define LIST_TOP 92
#define FOOTER_Y 222

/* A 16x4 solid (all index 1, opaque) 2bpp tile, tiled to draw the underline. */
#define BAR_SEG_W 16
#define BAR_SEG_H 4
static uint8_t s_bar_tile[(BAR_SEG_W * BAR_SEG_H * 2) / 8];

/* Scaled-title glyph pool: 12 glyphs * 64 B (16x16 @ 2bpp) fits in 1 KB. */
#define TITLE_POOL_SIZE 1024U
static uint8_t s_title_pool[TITLE_POOL_SIZE];

/* One UI layer holds the whole menu; sized well above the realistic glyph count. */
#define MENU_MAX_SPRITES 256U
static Sprite s_ui[MENU_MAX_SPRITES];

static char s_names[MENU_MAX_GAMES][MENU_NAME_CHARS + 1U];
static uint32_t s_num_games = 0U;
static uint32_t s_selected = 0U;

/* Short navigation blips (kept in flash so the buzzer's stored pointer stays valid). */
static const uint16_t s_move_notes[] = {NOTE_A5, 24U};
static const uint16_t s_select_notes[] = {NOTE_E5, 40U, NOTE_A5, 60U};

static uint16_t glyphAdvance(FontSize size)
{
    return (uint16_t)(fontGlyphW(size) + 1U);
}

static uint16_t textWidth(FontSize size, const char *text)
{
    uint16_t n = (uint16_t)strlen(text);
    return (n == 0U) ? 0U : (uint16_t)(n * glyphAdvance(size) - 1U);
}

/* Emit a string as one sprite per glyph into s_ui; returns the next free index. */
static uint16_t drawText(uint16_t idx, const Font *font, int16_t x, int16_t y,
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
            s_ui[idx++] = (Sprite){.x = x, .y = y, .w = gw, .h = gh, .z = 1U,
                                   .flags = 0U, .pixels = pixels, .palette = palette};
        }
        x = (int16_t)(x + glyphAdvance(size));
    }
    return idx;
}

/* Like drawText but nearest-neighbour scaled; glyph pixels are baked into the pool. */
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
            s_ui[idx++] = (Sprite){.x = x, .y = y, .w = sw, .h = sh, .z = 1U,
                                   .flags = 0U, .pixels = cursor, .palette = palette};
            cursor += slot;
            remaining = (uint16_t)(remaining - slot);
        }
        x = (int16_t)(x + sw + factor);
    }
    return idx;
}

/* A horizontal accent bar of `width` px, centered on screen at row `y`. */
static uint16_t drawBar(uint16_t idx, int16_t x, int16_t y, uint16_t width)
{
    for (int16_t sx = x; sx < (int16_t)(x + width) && idx < MENU_MAX_SPRITES; sx += BAR_SEG_W)
    {
        s_ui[idx++] = (Sprite){.x = sx, .y = y, .w = BAR_SEG_W, .h = BAR_SEG_H, .z = 0U,
                               .flags = SPRITE_OPAQUE, .pixels = s_bar_tile, .palette = s_pal_accent};
    }
    return idx;
}

/* Display name for a binary index: filename with the extension stripped, capped. */
static void cacheGameName(uint32_t index, char *out)
{
    char full[FF_LFN_BUF];
    uint32_t len = 0U;
    out[0] = '\0';
    if (loaderGetFilenameByIndex(index, full, &len) != FR_OK)
    {
        return;
    }
    char *dot = strrchr(full, '.');
    if (dot != NULL)
    {
        *dot = '\0';
    }
    strncpy(out, full, MENU_NAME_CHARS);
    out[MENU_NAME_CHARS] = '\0';
}

void mainMenuInit(void)
{
    rendererInit();
    rendererSetBackground(COL_BG);

    memset(s_bar_tile, 0x55, sizeof(s_bar_tile)); /* 0x55 = four index-1 pixels per byte */

    s_num_games = loaderGetBinaryFilesNumberInDirectory();
    if (s_num_games > MENU_MAX_GAMES)
    {
        s_num_games = MENU_MAX_GAMES;
    }
    for (uint32_t i = 0U; i < s_num_games; i++)
    {
        cacheGameName(i, s_names[i]);
    }
    s_selected = 0U;

    LOGGER_LOG_INFO(LOGGER_MENU, "menu ready, %lu game(s)", (unsigned long)s_num_games);
}

void mainMenuUpdate(void)
{
    static uint32_t last_nav = 0U;
    static uint32_t last_select = 0U;
    const uint32_t DEBOUNCE_MS = 220U;
    const uint32_t now = getSysTime();

    if (s_num_games == 0U)
    {
        return;
    }

    const bool down = joystickGetLBtnDown() || joystickGetRBtnDown();
    const bool up = joystickGetLBtnUp() || joystickGetRBtnUp();

    if ((down || up) && (now > last_nav + DEBOUNCE_MS))
    {
        if (down && (s_selected + 1U < s_num_games))
        {
            s_selected++;
            last_nav = now;
            buzzerPlay(0U, false, s_move_notes, 1U);
        }
        else if (up && (s_selected > 0U))
        {
            s_selected--;
            last_nav = now;
            buzzerPlay(0U, false, s_move_notes, 1U);
        }
    }

    if (joystickGetSpecialBtn1() && (now > last_select + DEBOUNCE_MS))
    {
        last_select = now;
        buzzerPlay(0U, false, s_select_notes, 2U);
        LOGGER_LOG_INFO(LOGGER_MENU, "launching '%s'", s_names[s_selected]);
        gameLoaderLoadGame((uint8_t)s_selected);
        mainMenuInit(); /* game returned: rebuild the menu over whatever it left */
    }
}

/* Which game sits at the top of the visible window, so the selection stays in view. */
static uint32_t firstVisibleRow(void)
{
    if (s_num_games <= MENU_VISIBLE_ROWS || s_selected < MENU_VISIBLE_ROWS / 2U)
    {
        return 0U;
    }
    uint32_t first = s_selected - MENU_VISIBLE_ROWS / 2U;
    if (first + MENU_VISIBLE_ROWS > s_num_games)
    {
        first = s_num_games - MENU_VISIBLE_ROWS;
    }
    return first;
}

void mainMenuRender(void)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const bool cursor_on = ((getSysTime() / 450U) & 1U) == 0U;
    uint16_t n = 0U;

    rendererClear();

    /* Title + underline. */
    const uint16_t title_w = (uint16_t)(strlen(TITLE_TEXT) * (fontGlyphW(font8x8.size) * TITLE_SCALE + TITLE_SCALE));
    const int16_t title_x = (int16_t)((screen_w - (int16_t)title_w) / 2);
    n = drawTextScaled(n, &font8x8, title_x, TITLE_Y, TITLE_SCALE, s_pal_title, TITLE_TEXT);
    n = drawBar(n, title_x, UNDERLINE_Y, title_w);

    if (s_num_games == 0U)
    {
        const char *msg = "No games on the SD card";
        const int16_t x = (int16_t)((screen_w - (int16_t)textWidth(font5x5.size, msg)) / 2);
        n = drawText(n, &font5x5, x, 120, s_pal_empty, msg);
    }
    else
    {
        const uint32_t first = firstVisibleRow();
        const uint32_t last = (first + MENU_VISIBLE_ROWS < s_num_games)
                                  ? first + MENU_VISIBLE_ROWS
                                  : s_num_games;

        for (uint32_t i = first; i < last; i++)
        {
            const bool selected = (i == s_selected);
            const int16_t y = (int16_t)(LIST_TOP + (int)(i - first) * MENU_ROW_H);
            const int16_t name_x = (int16_t)((screen_w - (int16_t)textWidth(font8x8.size, s_names[i])) / 2);

            if (selected && cursor_on)
            {
                /* Blinking cursor just left of the name. */
                n = drawText(n, &font8x8, (int16_t)(name_x - 18), y, s_pal_accent, ">");
            }
            n = drawText(n, &font8x8, name_x, y,
                         selected ? s_pal_item_sel : s_pal_item, s_names[i]);
        }
    }

    /* Footer hint. */
    const char *footer = (s_num_games == 0U) ? "insert an SD card with .bin games"
                                             : "UP / DOWN  browse        A  play";
    const int16_t fx = (int16_t)((screen_w - (int16_t)textWidth(font5x5.size, footer)) / 2);
    n = drawText(n, &font5x5, fx, FOOTER_Y, s_pal_footer, footer);

    rendererSubmitLayer(LAYER_UI, s_ui, n);
    rendererRender();
}
