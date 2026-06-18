#include "main_menu.h"
#include "menu_common.h"
#include "game_list.h"
#include "settings_menu.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"
#include <string.h>

/* ------------------------------------------------------------------ *
 *  Menu orchestrator. Owns the active screen and the root menu
 *  (Games / Settings / Poll Remote Games), dispatches input and
 *  drawing to the active screen, and applies the screen transitions
 *  each screen requests. The game picker and settings tree live in
 *  their own modules; the remote-games stub is small enough to live
 *  here until the network path is implemented.
 * ------------------------------------------------------------------ */

typedef enum
{
    SCREEN_ROOT,
    SCREEN_GAMES,
    SCREEN_SETTINGS,
    SCREEN_REMOTE
} MenuScreen;

static MenuScreen s_screen;

/* Root menu items map one-to-one onto screen transitions. */
typedef struct
{
    const char *label;
    MenuTransition target;
} RootItem;

static const RootItem s_root_items[] = {
    {"Games", MENU_GOTO_GAMES},
    {"Settings", MENU_GOTO_SETTINGS},
    {"Poll Remote Games", MENU_GOTO_REMOTE},
};
#define ROOT_ITEM_COUNT (sizeof(s_root_items) / sizeof(s_root_items[0]))

static uint32_t s_root_selected;

static const uint16_t s_move_notes[] = {NOTE_A5, 24U};
static const uint16_t s_select_notes[] = {NOTE_E5, 40U, NOTE_A5, 60U};

/* ---- Root menu ---- */

static MenuTransition rootUpdate(void)
{
    const MenuNav nav = menuPollNav();

    if (nav.down && (s_root_selected + 1U < ROOT_ITEM_COUNT))
    {
        s_root_selected++;
        buzzerPlay(0U, false, s_move_notes, 1U);
    }
    else if (nav.up && (s_root_selected > 0U))
    {
        s_root_selected--;
        buzzerPlay(0U, false, s_move_notes, 1U);
    }
    else if (nav.enter)
    {
        buzzerPlay(0U, false, s_select_notes, 2U);
        return s_root_items[s_root_selected].target;
    }
    return MENU_STAY; /* root is home: Special Button 2 does nothing here */
}

static void rootRender(void)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const bool cursor_on = ((getSysTime() / 450U) & 1U) == 0U;
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "GAME CONSOLE");

    for (uint32_t i = 0U; i < ROOT_ITEM_COUNT; i++)
    {
        const bool selected = (i == s_root_selected);
        const char *label = s_root_items[i].label;
        const int16_t y = (int16_t)(MENU_LIST_TOP + (int)i * MENU_ROW_H);
        const int16_t x = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, label)) / 2);

        if (selected && cursor_on)
        {
            n = menuDrawText(n, &font8x8, (int16_t)(x - 18), y, g_menu_pal_accent, ">");
        }
        n = menuDrawText(n, &font8x8, x, y,
                         selected ? g_menu_pal_item_sel : g_menu_pal_item, label);
    }

    n = menuDrawFooter(n, "UP/DOWN browse   A select");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

/* ---- Poll Remote Games (stub) ---- */

static void remoteEnter(void)
{
    menuResetSurface();
}

static MenuTransition remoteUpdate(void)
{
    const MenuNav nav = menuPollNav();
    return nav.back ? MENU_GOTO_ROOT : MENU_STAY;
}

static void remoteRender(void)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "REMOTE GAMES");

    const char *msg = "Coming soon";
    const int16_t x = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, msg)) / 2);
    n = menuDrawText(n, &font8x8, x, 120, g_menu_pal_item, msg);

    n = menuDrawFooter(n, "remote game polling is not yet available    B back");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

/* ---- Orchestration ---- */

static void enterScreen(MenuScreen screen)
{
    s_screen = screen;
    switch (screen)
    {
    case SCREEN_ROOT:
        menuResetSurface();
        break;
    case SCREEN_GAMES:
        gameListEnter();
        break;
    case SCREEN_SETTINGS:
        settingsMenuEnter();
        break;
    case SCREEN_REMOTE:
        remoteEnter();
        break;
    }
}

static void applyTransition(MenuTransition transition)
{
    switch (transition)
    {
    case MENU_STAY:
        break;
    case MENU_GOTO_ROOT:
        enterScreen(SCREEN_ROOT);
        break;
    case MENU_GOTO_GAMES:
        enterScreen(SCREEN_GAMES);
        break;
    case MENU_GOTO_SETTINGS:
        enterScreen(SCREEN_SETTINGS);
        break;
    case MENU_GOTO_REMOTE:
        enterScreen(SCREEN_REMOTE);
        break;
    }
}

void mainMenuInit(void)
{
    rendererInit();
    menuCommonInit();
    s_root_selected = 0U;
    enterScreen(SCREEN_ROOT);
    LOGGER_LOG_INFO(LOGGER_MENU, "menu ready");
}

void mainMenuUpdate(void)
{
    MenuTransition transition = MENU_STAY;

    switch (s_screen)
    {
    case SCREEN_ROOT:
        transition = rootUpdate();
        break;
    case SCREEN_GAMES:
        transition = gameListUpdate();
        break;
    case SCREEN_SETTINGS:
        transition = settingsMenuUpdate();
        break;
    case SCREEN_REMOTE:
        transition = remoteUpdate();
        break;
    }

    applyTransition(transition);
}

void mainMenuRender(void)
{
    switch (s_screen)
    {
    case SCREEN_ROOT:
        rootRender();
        break;
    case SCREEN_GAMES:
        gameListRender();
        break;
    case SCREEN_SETTINGS:
        settingsMenuRender();
        break;
    case SCREEN_REMOTE:
        remoteRender();
        break;
    }
}
