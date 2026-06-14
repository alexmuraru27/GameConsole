#include "main_menu.h"
#include <stddef.h>
#include "renderer.h"
#include "loader.h"
#include "game_loader.h"
#include "string.h"
#include "joystick.h"
#include "sysclock.h"

#define FONT_PALETTE_IDX 1U
#define FONT_CHAR_PALETTE '0'
#define MAX_STRING_CHARS_DRAWN 16

static uint32_t s_num_binary_files = 0U;
static uint32_t s_current_highlighted_game = 0U;
static bool s_is_to_redraw_game_menu = false;

void mainMenuInit(void)
{
    rendererInit();

    s_is_to_redraw_game_menu = true;
    s_num_binary_files = loaderGetBinaryFilesNumberInDirectory();
    s_current_highlighted_game = 0U;
}

static bool isGameIndexValid(const uint32_t game_index)
{
    return ((s_num_binary_files > 0U) && (game_index < s_num_binary_files));
}

static void computeSelectedGame()
{
    // SW debounce
    static uint32_t sys_tick_value_down = 0U;
    static uint32_t sys_tick_value_up = 0U;
    static uint32_t sys_tick_value_select = 0U;
    const uint32_t DEBOUNCE_TIME_MS = 500U;
    const uint32_t sys_tick_time = getSysTime();

    if ((joystickGetLBtnDown() || joystickGetRBtnDown()) && (sys_tick_time > (sys_tick_value_down + DEBOUNCE_TIME_MS)))
    {
        sys_tick_value_down = sys_tick_time;
        if ((s_current_highlighted_game + 1U) < s_num_binary_files)
        {
            s_current_highlighted_game++;
            s_is_to_redraw_game_menu = true;
        }
    }

    if ((joystickGetLBtnUp() || joystickGetRBtnUp()) && (sys_tick_time > (sys_tick_value_up + DEBOUNCE_TIME_MS)))
    {
        sys_tick_value_up = sys_tick_time;
        if (s_current_highlighted_game > 0U)
        {
            s_current_highlighted_game--;
            s_is_to_redraw_game_menu = true;
        }
    }

    if (joystickGetSpecialBtn1() && (sys_tick_time > (sys_tick_value_select + DEBOUNCE_TIME_MS)))
    {
        sys_tick_value_select = sys_tick_time;
        if (isGameIndexValid(s_current_highlighted_game))
        {
            gameLoaderLoadGame(s_current_highlighted_game);
            mainMenuInit();
        }
    }
}

void mainMenuUpdate(void)
{
    computeSelectedGame();
}