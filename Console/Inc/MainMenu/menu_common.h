#ifndef __MENU_COMMON_H
#define __MENU_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "Renderer/renderer.h"
#include "Fonts/fonts.h"

/* ------------------------------------------------------------------ *
 *  Shared infrastructure for the console menu screens: the slate+cyan
 *  theme, the single UI sprite buffer (only one screen composes at a
 *  time), the text/title/bar draw helpers, and a debounced joystick
 *  poll. Each screen module builds its frame into g_menu_ui through
 *  these helpers; main_menu.c drives which screen is active.
 * ------------------------------------------------------------------ */

#define MENU_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Slate + cyan theme. */
#define MENU_COL_BG MENU_RGB(16, 20, 28)
#define MENU_COL_ACCENT MENU_RGB(72, 209, 226)    /* cyan: underline + cursor */
#define MENU_COL_TITLE MENU_RGB(236, 244, 248)     /* near-white title */
#define MENU_COL_ITEM MENU_RGB(112, 132, 156)      /* unselected row */
#define MENU_COL_ITEM_SEL MENU_RGB(156, 232, 244)  /* selected row */
#define MENU_COL_FOOTER MENU_RGB(84, 102, 124)
#define MENU_COL_EMPTY MENU_RGB(150, 120, 120)
#define MENU_COL_ALERT MENU_RGB(232, 72, 72) /* red: missing SD / error state */

/* 2bpp font palettes (slot 0 transparent, 1-3 the ink). */
extern const uint16_t g_menu_pal_title[4];
extern const uint16_t g_menu_pal_accent[4];
extern const uint16_t g_menu_pal_item[4];
extern const uint16_t g_menu_pal_item_sel[4];
extern const uint16_t g_menu_pal_footer[4];
extern const uint16_t g_menu_pal_empty[4];
extern const uint16_t g_menu_pal_alert[4];

/* One UI layer holds the whole screen; sized well above the glyph count. */
#define MENU_MAX_SPRITES 256U
extern Sprite g_menu_ui[MENU_MAX_SPRITES];

/* A screen's update returns where the orchestrator should go next; STAY keeps
 * the current screen. Sub-screens return GOTO_ROOT to climb back to the root. */
typedef enum
{
    MENU_STAY,
    MENU_GOTO_ROOT,
    MENU_GOTO_GAMES,
    MENU_GOTO_SETTINGS,
    MENU_GOTO_REMOTE,
    MENU_REBOOT
} MenuTransition;

/* Shared layout anchors (px). */
#define MENU_LIST_TOP 92
#define MENU_ROW_H 20
#define MENU_FOOTER_Y 222

/* Blinking selection cursor: the ">" chevron is shown while menuCursorVisible()
 * is true, and sits MENU_CURSOR_DX px left of the row it marks. One period for
 * every screen (the keyboard caret keeps its own, see keyboard.c). */
#define MENU_BLINK_MS 450U
#define MENU_CURSOR_DX 18

/* True on the "on" half of the blink period — gate the selection chevron on this. */
bool menuCursorVisible(void);

/* Build the shared assets (the underline tile). Call once at boot. */
void menuCommonInit(void);

/* rendererSetBackground(MENU_COL_BG) — re-establish the surface on screen entry. */
void menuResetSurface(void);

/* Pixel width of `text` rendered in the given font size. */
uint16_t menuTextWidth(FontSize size, const char *text);

/* Draw `text` at (x,y) in the given font, tinted palette[1]. Routes through the
 * renderer's console-side text path, so it consumes no g_menu_ui slot and takes
 * no sprite index (unlike the bar/gauge emitters below). */
void menuDrawText(const Font *font, int16_t x, int16_t y,
                  const uint16_t *palette, const char *text);

/* Centered, scaled title + cyan underline at the top of the screen. The title
 * text is slot-free, but the underline is a real bar sprite, so this still
 * threads the g_menu_ui index (pass 0 first, use the return for later sprites). */
uint16_t menuDrawTitle(uint16_t idx, const char *text);

/* Centered footer hint at MENU_FOOTER_Y (font5x5). Text only — no sprite index. */
void menuDrawFooter(const char *text);

/* A horizontal solid bar `width` px wide at (x,y), in `palette`'s ink.
 * One tile row tall (MENU_BAR_H px); stack rows for a thicker bar. */
#define MENU_BAR_H 4
uint16_t menuDrawBar(uint16_t idx, int16_t x, int16_t y, uint16_t width, const uint16_t *palette);

/* ------------------------------------------------------------------ *
 *  Debounced navigation. menuPollNav() latches at most one edge per
 *  debounce window across all screens, so a held button (or a button
 *  carried across a screen transition) fires once, not every frame.
 * ------------------------------------------------------------------ */
typedef struct
{
    bool up;
    bool down;
    bool left;  /* decrement a slider / numeric setting */
    bool right; /* increment a slider / numeric setting */
    bool enter; /* Special Button 1: select / confirm / toggle */
    bool back;  /* Special Button 2: step back a level */
} MenuNav;

MenuNav menuPollNav(void);

/* Scrolling-list state: the highlighted item and the first visible row. */
typedef struct
{
    int selected;
    int top;
} MenuListState;

/* Apply an up/down nav to a list of `count` items showing `visible_rows` at once:
 * move the (clamped) selection, then scroll the viewport to keep it visible.
 * Returns true if the selection actually moved (so the caller can beep). */
bool menuListStep(MenuListState *state, MenuNav nav, int count, int visible_rows);

/* The standard selection-move beep (one short tick), shared by every list screen. */
void menuBeepMove(void);

/* ------------------------------------------------------------------ *
 *  A segmented level gauge (e.g. a brightness slider): `total` cells
 *  drawn left-to-right at (x,y), the first `filled` in the accent ink
 *  and the rest dim. Returns the next free g_menu_ui index.
 * ------------------------------------------------------------------ */
#define MENU_GAUGE_CELL_W 5
#define MENU_GAUGE_CELL_GAP 1

/* Pixel width of a `total`-cell gauge (for right-aligning it on a row). */
uint16_t menuGaugeWidth(uint8_t total);
uint16_t menuDrawGauge(uint16_t idx, int16_t x, int16_t y, uint8_t total, uint8_t filled);

/* ------------------------------------------------------------------ *
 *  Shared modal / progress vocabulary, used by the download and
 *  firmware-flash screens (download_ui.c, flash_ui.c) so the bar, the
 *  centering, and the wait-for-back loop live in one place.
 * ------------------------------------------------------------------ */

/* Left X to horizontally center `text` (in `size`) on screen. */
int16_t menuCenterX(FontSize size, const char *text);

/* Draw `text` horizontally centered on row y, tinted palette[1]. */
void menuDrawTextCentered(const Font *font, int16_t y, const uint16_t *palette, const char *text);

/* A stacked progress bar at (x,y): `rows` MENU_BAR_H-tall rows, each a dim track
 * with an accent fill proportional to done/total, `w` px wide. Returns next idx. */
uint16_t menuDrawProgressBar(uint16_t idx, int16_t x, int16_t y, uint16_t w, uint8_t rows,
                             uint32_t done, uint32_t total);

/* One-frame modal: centered `line` under `title` (no input). */
void menuModalInfo(const char *title, const char *line, const uint16_t *palette);

/* Modal: centered `line` under `title` + a "back" footer; blocks until Special
 * Button 2. */
void menuModalWaitBack(const char *title, const char *line, const uint16_t *palette);

#endif /* __MENU_COMMON_H */
