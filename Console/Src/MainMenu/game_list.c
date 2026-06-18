#include "game_list.h"
#include "renderer.h"
#include "loader.h"
#include "game_loader.h"
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

void gameListEnter(void)
{
    menuResetSurface();

    s_num_games = loaderGetBinaryFilesNumberInDirectory();
    if (s_num_games > GL_MAX_GAMES)
    {
        s_num_games = GL_MAX_GAMES;
    }
    for (uint32_t i = 0U; i < s_num_games; i++)
    {
        cacheGameName(i, s_names[i]);
    }
    s_selected = 0U;

    LOGGER_LOG_INFO(LOGGER_MENU, "game list ready, %lu game(s)", (unsigned long)s_num_games);
}

MenuTransition gameListUpdate(void)
{
    const MenuNav nav = menuPollNav();

    if (nav.back)
    {
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
    }
    else if (nav.up && (s_selected > 0U))
    {
        s_selected--;
        buzzerPlay(0U, false, s_move_notes, 1U);
    }
    else if (nav.enter)
    {
        buzzerPlay(0U, false, s_select_notes, 2U);
        LOGGER_LOG_INFO(LOGGER_MENU, "launching '%s'", s_names[s_selected]);
        const uint8_t result = gameLoaderLoadGame((uint8_t)s_selected);
        /* Game returned: it owned the renderer, so rebuild the surface. */
        rendererInit();
        menuResetSurface();
        if (result == GAME_LOADER_RET_CRASHED)
        {
            s_crash_banner = true;
            s_crash_banner_until = getSysTime() + GL_CRASH_BANNER_MS;
            strncpy(s_crash_name, s_names[s_selected], sizeof(s_crash_name) - 1U);
            s_crash_name[sizeof(s_crash_name) - 1U] = '\0';
        }
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
    const bool cursor_on = ((getSysTime() / 450U) & 1U) == 0U;
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "GAMES");

    if (s_crash_banner)
    {
        if (getSysTime() < s_crash_banner_until)
        {
            char banner[GL_NAME_CHARS + 24U];
            snprintf(banner, sizeof(banner), "%s crashed - recovered", s_crash_name);
            const int16_t x = (int16_t)((screen_w - (int16_t)menuTextWidth(font5x5.size, banner)) / 2);
            n = menuDrawText(n, &font5x5, x, 30, g_menu_pal_accent, banner);
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
        n = menuDrawText(n, &font5x5, x, 120, g_menu_pal_empty, msg);
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
                n = menuDrawText(n, &font8x8, (int16_t)(name_x - 18), y, g_menu_pal_accent, ">");
            }
            n = menuDrawText(n, &font8x8, name_x, y,
                             selected ? g_menu_pal_item_sel : g_menu_pal_item, s_names[i]);
        }
    }

    const char *footer = (s_num_games == 0U) ? "insert an SD card with .bin games"
                                             : "UP/DOWN browse   A play   B back";
    n = menuDrawFooter(n, footer);

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}
