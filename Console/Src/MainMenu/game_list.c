#include "game_list.h"
#include "renderer.h"
#include "loader.h"
#include "game_loader.h"
#include "crash_report.h"
#include "joystick.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ *
 *  Centered-hero game picker. Behaviourally identical to the original
 *  single-screen menu, now one screen among several: it draws into the
 *  shared g_menu_ui buffer and reads input through menuPollNav().
 * ------------------------------------------------------------------ */

#define GL_MAX_GAMES 32U
#define GL_NAME_CHARS 20U /* displayed game-name cap (excludes the .bin) */
#define GL_VISIBLE_ROWS 6U

static char s_names[GL_MAX_GAMES][GL_NAME_CHARS + 1U];
static uint32_t s_num_games = 0U;
static uint32_t s_selected = 0U;

/* After a game crashes the console recovers and returns here; show a short banner
 * so the player learns why they were dropped back to the menu. */
static bool s_crash_banner = false;
static uint32_t s_crash_banner_until = 0U;
static char s_crash_name[GL_NAME_CHARS + 1U];
#define GL_CRASH_BANNER_MS 4000U

/* Delete flow: hold Special Button 1 to arm a delete, confirmed on a second screen.
 * Play is driven off the button's *release* (a short tap) so the same button can
 * both launch (tap) and delete (hold) without the press edge launching first. */
#define GL_DELETE_HOLD_MS 700U
static uint32_t s_a_press_ms = 0U;
static bool s_a_pressing = false;   /* an SB1 press began on this screen (not held in) */
static bool s_a_hold_fired = false; /* the hold threshold already fired for this press */
static bool s_confirm_delete = false;

/* Short navigation blips (kept in flash so the buzzer's stored pointer stays valid). */
static const uint16_t s_move_notes[] = {NOTE_A5, 24U};
static const uint16_t s_select_notes[] = {NOTE_E5, 40U, NOTE_A5, 60U};

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
    strncpy(out, full, GL_NAME_CHARS);
    out[GL_NAME_CHARS] = '\0';
}

/* (Re)enumerate the Games/ directory into s_names, clamping the selection to the
 * new count. Used on entry and again after a delete removes a row. */
static void loadGameNames(void)
{
    s_num_games = loaderGetBinaryFilesNumberInDirectory();
    if (s_num_games > GL_MAX_GAMES)
    {
        s_num_games = GL_MAX_GAMES;
    }
    for (uint32_t i = 0U; i < s_num_games; i++)
    {
        cacheGameName(i, s_names[i]);
    }
    if (s_selected >= s_num_games)
    {
        s_selected = (s_num_games > 0U) ? (s_num_games - 1U) : 0U;
    }
}

void gameListEnter(void)
{
    menuResetSurface();

    s_selected = 0U;
    loadGameNames();

    s_confirm_delete = false;
    s_a_pressing = false;
    s_a_hold_fired = false;

    LOGGER_LOG_INFO(LOGGER_MENU, "game list ready, %lu game(s)", (unsigned long)s_num_games);
}

/* Launch the highlighted game (blocks for its lifetime), then rebuild the picker's
 * surface and, on a crash, arm the recovery banner. */
static void playSelected(void)
{
    s_crash_banner = false; /* dismiss any lingering banner before this launch */
    buzzerPlay(0U, false, s_select_notes, 2U);
    LOGGER_LOG_INFO(LOGGER_MENU, "game start: '%s'", s_names[s_selected]);
    const uint8_t result = gameLoaderLoadGame((uint8_t)s_selected);
    /* Game returned: it owned the renderer, so rebuild the surface. */
    rendererInit();
    menuResetSurface();
    if (result == GAME_LOADER_RET_CRASHED)
    {
        LOGGER_LOG_WARN(LOGGER_MENU, "game end: '%s' crashed", s_names[s_selected]);
        s_crash_banner = true;
        s_crash_banner_until = getSysTime() + GL_CRASH_BANNER_MS;
        strncpy(s_crash_name, s_names[s_selected], sizeof(s_crash_name) - 1U);
        s_crash_name[sizeof(s_crash_name) - 1U] = '\0';
    }
    else
    {
        LOGGER_LOG_INFO(LOGGER_MENU, "game end: '%s' returned (code %u)", s_names[s_selected], (unsigned)result);
    }
}

MenuTransition gameListUpdate(void)
{
    const uint32_t now = getSysTime();
    const MenuNav nav = menuPollNav();
    InputState in;
    joystickGetState(&in); /* raw edges/held for the tap-vs-hold Special Button 1 */

    /* The delete confirmation owns input while it is open. It waits for a fresh SB1
     * press edge (the button is still held from the long-press when it opens, so
     * `pressed` — a rising edge — cannot auto-confirm), and SB2 cancels rather than
     * exiting the picker. */
    if (s_confirm_delete)
    {
        if (in.special2.pressed)
        {
            s_confirm_delete = false;
            buzzerPlay(0U, false, s_move_notes, 1U);
            LOGGER_LOG_INFO(LOGGER_MENU, "games: delete cancelled");
        }
        else if (in.special1.pressed)
        {
            LOGGER_LOG_INFO(LOGGER_MENU, "games: deleting '%s'", s_names[s_selected]);
            loaderDeleteGame(s_selected);
            loadGameNames();
            s_confirm_delete = false;
            buzzerPlay(0U, false, s_select_notes, 2U);
        }
        return MENU_STAY;
    }

    if (nav.back)
    {
        LOGGER_LOG_DEBUG(LOGGER_MENU, "games: back to root");
        return MENU_GOTO_ROOT;
    }

    if (s_num_games == 0U)
    {
        return MENU_STAY;
    }

    if (nav.down && (s_selected + 1U < s_num_games))
    {
        s_selected++;
        buzzerPlay(0U, false, s_move_notes, 1U);
        LOGGER_LOG_DEBUG(LOGGER_MENU, "games: highlight '%s'", s_names[s_selected]);
    }
    else if (nav.up && (s_selected > 0U))
    {
        s_selected--;
        buzzerPlay(0U, false, s_move_notes, 1U);
        LOGGER_LOG_DEBUG(LOGGER_MENU, "games: highlight '%s'", s_names[s_selected]);
    }

    /* Special Button 1: a short tap plays, a long hold arms the delete. Both are
     * driven from the raw held state (not nav.enter) so the press edge doesn't
     * launch before a hold can register. s_a_pressing gates on a press that *began*
     * here, so a button held across a game->menu return never counts. */
    if (in.special1.pressed)
    {
        s_a_press_ms = now;
        s_a_pressing = true;
        s_a_hold_fired = false;
    }
    if (s_a_pressing && in.special1.held && !s_a_hold_fired &&
        (now - s_a_press_ms) >= GL_DELETE_HOLD_MS)
    {
        s_a_hold_fired = true;
        s_a_pressing = false;
        s_confirm_delete = true;
        buzzerPlay(0U, false, s_select_notes, 2U);
        LOGGER_LOG_INFO(LOGGER_MENU, "games: hold-delete armed for '%s'", s_names[s_selected]);
    }
    if (in.special1.released)
    {
        if (s_a_pressing && !s_a_hold_fired)
        {
            playSelected();
        }
        s_a_pressing = false;
    }

    return MENU_STAY;
}

/* Which game sits at the top of the visible window, so the selection stays in view. */
static uint32_t firstVisibleRow(void)
{
    if (s_num_games <= GL_VISIBLE_ROWS || s_selected < GL_VISIBLE_ROWS / 2U)
    {
        return 0U;
    }
    uint32_t first = s_selected - GL_VISIBLE_ROWS / 2U;
    if (first + GL_VISIBLE_ROWS > s_num_games)
    {
        first = s_num_games - GL_VISIBLE_ROWS;
    }
    return first;
}

void gameListRender(void)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const bool cursor_on = menuCursorVisible();
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "GAMES");

    /* Delete confirmation replaces the list until the player answers. */
    if (s_confirm_delete)
    {
        const char *q = "DELETE THIS GAME?";
        const int16_t qx = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, q)) / 2);
        menuDrawText(&font8x8, qx, 104, g_menu_pal_alert, q);

        const int16_t nx = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, s_names[s_selected])) / 2);
        menuDrawText(&font8x8, nx, 132, g_menu_pal_item_sel, s_names[s_selected]);

        menuDrawFooter("A delete   B cancel");
        rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
        rendererRender();
        return;
    }

    if (s_crash_banner)
    {
        if (getSysTime() < s_crash_banner_until)
        {
            /* Two lines below the title underline: which game, then the decoded cause
             * (e.g. "PC 0x2001A3F4 DACCVIOL") — the same detail written to crash.log. */
            char line1[GL_NAME_CHARS + 12U];
            snprintf(line1, sizeof(line1), "%s crashed", s_crash_name);
            const int16_t x1 = (int16_t)((screen_w - (int16_t)menuTextWidth(font5x5.size, line1)) / 2);
            menuDrawText(&font5x5, x1, 60, g_menu_pal_alert, line1);

            char line2[48];
            crashReportFormatBanner(line2, sizeof(line2));
            const int16_t x2 = (int16_t)((screen_w - (int16_t)menuTextWidth(font5x5.size, line2)) / 2);
            menuDrawText(&font5x5, x2, 72, g_menu_pal_accent, line2);
        }
        else
        {
            s_crash_banner = false;
        }
    }

    if (s_num_games == 0U)
    {
        const char *msg = "No games on the SD card";
        const int16_t x = (int16_t)((screen_w - (int16_t)menuTextWidth(font5x5.size, msg)) / 2);
        menuDrawText(&font5x5, x, 120, g_menu_pal_empty, msg);
    }
    else
    {
        const uint32_t first = firstVisibleRow();
        const uint32_t last = (first + GL_VISIBLE_ROWS < s_num_games)
                                  ? first + GL_VISIBLE_ROWS
                                  : s_num_games;

        for (uint32_t i = first; i < last; i++)
        {
            const bool selected = (i == s_selected);
            const int16_t y = (int16_t)(MENU_LIST_TOP + (int)(i - first) * MENU_ROW_H);
            const int16_t name_x = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, s_names[i])) / 2);

            if (selected && cursor_on)
            {
                menuDrawText(&font8x8, (int16_t)(name_x - MENU_CURSOR_DX), y, g_menu_pal_accent, ">");
            }
            menuDrawText(&font8x8, name_x, y,
                             selected ? g_menu_pal_item_sel : g_menu_pal_item, s_names[i]);
        }
    }

    const char *footer = (s_num_games == 0U) ? "insert an SD card with .bin games"
                                             : "UP/DOWN browse   A play   HOLD A delete   B back";
    menuDrawFooter(footer);

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}
