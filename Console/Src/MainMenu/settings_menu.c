#include "MainMenu/settings_menu.h"
#include "Renderer/renderer.h"
#include "Devices/buzzer.h"
#include "Devices/backlight.h"
#include "SettingsStorage/console_settings_storage.h"
#include "Peripherals/sysclock.h"
#include "Fonts/fonts.h"
#include "Logger/logger.h"
#include "MainMenu/wifi_update.h"
#include "MainMenu/os_update.h"
#include "MainMenu/wifi_menu.h"
#include "MainMenu/remote_update.h"
#include "MainMenu/keyboard.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ *
 *  Tree-like settings. The screen renders one level of a static
 *  SettingNode tree at a time and walks it with a small frame stack.
 *  Today the tree is a single buzzer-sound toggle; future leaves
 *  (console name, network category with IP / WiFi name / password)
 *  drop in as data below — no control-flow changes required.
 * ------------------------------------------------------------------ */

typedef enum
{
    SETTING_CATEGORY, /* a node with children; entering descends into it */
    SETTING_TOGGLE,   /* a bool leaf; entering flips it */
    SETTING_ACTION,   /* a leaf that runs a blocking action when entered */
    SETTING_NUMBER,   /* a numeric leaf; left/right step it within [min,max] */
    /* future: SETTING_TEXT */
} SettingKind;

typedef struct SettingNode
{
    const char *label;
    SettingKind kind;
    bool (*get)(void);            /* TOGGLE: read the current value */
    void (*set)(bool value);      /* TOGGLE: apply + persist the new value */
    void (*action)(void);         /* ACTION: run when entered */
    uint8_t (*num_get)(void);     /* NUMBER: read the current value */
    void (*num_set)(uint8_t val); /* NUMBER: apply + persist the new value */
    uint8_t num_min;              /* NUMBER: inclusive lower bound */
    uint8_t num_max;              /* NUMBER: inclusive upper bound */
    uint8_t num_step;             /* NUMBER: left/right increment */
    const struct SettingNode *children;
    uint8_t child_count; /* CATEGORY: number of children */
} SettingNode;

/* The console's settings live here; loaded at boot, written through on change. */
static ConsoleSettings s_console_settings;

/* ---- Buzzer-sound leaf (audio_enabled, persisted in ConsoleSettings) ---- */

static bool buzzerSoundGet(void)
{
    return s_console_settings.audio_enabled != 0U;
}

static void buzzerSoundSet(bool on)
{
    s_console_settings.audio_enabled = on ? 1U : 0U;
    consoleSettingsSave(&s_console_settings);
    buzzerSetMute(!on);
    LOGGER_LOG_INFO(LOGGER_SETTINGS, "buzzer sound %s", on ? "on" : "off");
}

/* ---- Brightness leaf (brightness, persisted in ConsoleSettings) ---- */

static uint8_t brightnessGet(void)
{
    return s_console_settings.brightness;
}

static void brightnessSet(uint8_t percent)
{
    backlightSetBrightness(percent);                             /* clamp/snap + apply */
    s_console_settings.brightness = backlightGetBrightness();    /* store the normalized value */
    consoleSettingsSave(&s_console_settings);
    LOGGER_LOG_INFO(LOGGER_SETTINGS, "brightness %u%%", (unsigned)s_console_settings.brightness);
}

/* ---- Player Name leaf (player_name, the multiplayer display name) ---- */

static void playerNameRun(void)
{
    menuResetSurface();

    char name[CONSOLE_PLAYER_NAME_SIZE];
    strncpy(name, s_console_settings.player_name, sizeof(name) - 1U); /* pre-fill current */
    name[sizeof(name) - 1U] = '\0';

    if (keyboardModal("PLAYER NAME", name, sizeof(name)))
    {
        menuResetSurface();
        strncpy(s_console_settings.player_name, name, sizeof(s_console_settings.player_name) - 1U);
        s_console_settings.player_name[sizeof(s_console_settings.player_name) - 1U] = '\0';
        consoleSettingsSave(&s_console_settings);
        LOGGER_LOG_INFO(LOGGER_SETTINGS, "player name -> '%s'", s_console_settings.player_name);
    }
}

/* ---- The tree ---- */

/* Display settings (today just the backlight brightness slider). */
static const SettingNode s_display_children[] = {
    {.label = "Brightness", .kind = SETTING_NUMBER, .num_get = brightnessGet, .num_set = brightnessSet,
     .num_min = BACKLIGHT_MIN_PERCENT, .num_max = BACKLIGHT_MAX_PERCENT, .num_step = BACKLIGHT_STEP_PERCENT},
};

/* WiFi connectivity settings live under one "WiFi" category. */
static const SettingNode s_wifi_children[] = {
    {.label = "Networks", .kind = SETTING_ACTION, .action = wifiMenuRun},
    {.label = "Server address", .kind = SETTING_ACTION, .action = remoteServerAddrRun},
};

/* Firmware upgrades (the console OS and the WiFi module) live together under
 * "Firmware" — both flash an image off the SD card. */
static const SettingNode s_firmware_children[] = {
    {.label = "Upgrade OS", .kind = SETTING_ACTION, .action = osUpdateRun},
    {.label = "Upgrade WiFi module", .kind = SETTING_ACTION, .action = wifiUpdateRun},
};

static const SettingNode s_root_children[] = {
    {.label = "Buzzer Sound", .kind = SETTING_TOGGLE, .get = buzzerSoundGet, .set = buzzerSoundSet},
    {.label = "Player Name", .kind = SETTING_ACTION, .action = playerNameRun},
    {.label = "Display", .kind = SETTING_CATEGORY, .children = s_display_children,
     .child_count = (uint8_t)(sizeof(s_display_children) / sizeof(s_display_children[0]))},
    {.label = "WiFi", .kind = SETTING_CATEGORY, .children = s_wifi_children,
     .child_count = (uint8_t)(sizeof(s_wifi_children) / sizeof(s_wifi_children[0]))},
    {.label = "Firmware", .kind = SETTING_CATEGORY, .children = s_firmware_children,
     .child_count = (uint8_t)(sizeof(s_firmware_children) / sizeof(s_firmware_children[0]))},
};

static const SettingNode s_root = {
    .label = "SETTINGS",
    .kind = SETTING_CATEGORY,
    .children = s_root_children,
    .child_count = (uint8_t)(sizeof(s_root_children) / sizeof(s_root_children[0])),
};

/* ---- Navigation: a stack of (category, selected child) frames ---- */

#define SETTINGS_MAX_DEPTH 4U

typedef struct
{
    const SettingNode *node; /* the category whose children are listed */
    uint8_t selected;        /* index of the highlighted child */
} NavFrame;

static NavFrame s_stack[SETTINGS_MAX_DEPTH];
static uint8_t s_depth; /* current frame index: s_stack[s_depth] */

/* Layout. */
#define ROW_LABEL_X 60
#define ROW_CURSOR_DX MENU_CURSOR_DX /* cursor sits this far left of the label */
#define ROW_RIGHT_PAD 60 /* right margin for a toggle's value column */

static const uint16_t s_toggle_notes[] = {NOTE_E5, 40U, NOTE_A5, 60U};

/* Step a SETTING_NUMBER by +/- its step, clamped to [min,max]. Applies + persists
 * only on an actual change, so holding against a limit doesn't re-write EEPROM. */
static void numberStep(const SettingNode *item, int dir)
{
    const uint8_t cur = item->num_get();
    int next = (int)cur + dir * (int)item->num_step;
    if (next < (int)item->num_min)
    {
        next = (int)item->num_min;
    }
    else if (next > (int)item->num_max)
    {
        next = (int)item->num_max;
    }
    if ((uint8_t)next != cur)
    {
        item->num_set((uint8_t)next);
        menuBeepMove();
    }
}

void settingsMenuEnter(void)
{
    menuResetSurface();
    /* Sync the editable copy with what's persisted (and applied) on the device. */
    consoleSettingsLoad(&s_console_settings); /* fills defaults on miss/corrupt */
    s_depth = 0U;
    s_stack[0].node = &s_root;
    s_stack[0].selected = 0U;
}

MenuTransition settingsMenuUpdate(void)
{
    NavFrame *const frame = &s_stack[s_depth];
    const SettingNode *const category = frame->node;
    const MenuNav nav = menuPollNav();

    if (nav.back)
    {
        if (s_depth == 0U)
        {
            LOGGER_LOG_DEBUG(LOGGER_MENU, "settings: back to root");
            return MENU_GOTO_ROOT;
        }
        s_depth--;
        LOGGER_LOG_DEBUG(LOGGER_MENU, "settings: back to '%s'", s_stack[s_depth].node->label);
        return MENU_STAY;
    }

    if (category->child_count == 0U)
    {
        return MENU_STAY;
    }

    if (nav.down && (frame->selected + 1U < category->child_count))
    {
        frame->selected++;
        menuBeepMove();
        LOGGER_LOG_DEBUG(LOGGER_MENU, "settings: highlight '%s'", category->children[frame->selected].label);
    }
    else if (nav.up && (frame->selected > 0U))
    {
        frame->selected--;
        menuBeepMove();
        LOGGER_LOG_DEBUG(LOGGER_MENU, "settings: highlight '%s'", category->children[frame->selected].label);
    }
    else if (nav.left && category->children[frame->selected].kind == SETTING_NUMBER)
    {
        numberStep(&category->children[frame->selected], -1);
    }
    else if (nav.right && category->children[frame->selected].kind == SETTING_NUMBER)
    {
        numberStep(&category->children[frame->selected], +1);
    }
    else if (nav.enter)
    {
        const SettingNode *const item = &category->children[frame->selected];
        if (item->kind == SETTING_TOGGLE)
        {
            item->set(!item->get());
            buzzerPlay(0U, false, s_toggle_notes, 2U);
            LOGGER_LOG_INFO(LOGGER_MENU, "settings: '%s' -> %s", item->label, item->get() ? "ON" : "OFF");
        }
        else if (item->kind == SETTING_ACTION && item->action)
        {
            buzzerPlay(0U, false, s_toggle_notes, 2U);
            LOGGER_LOG_INFO(LOGGER_MENU, "settings: run '%s'", item->label);
            item->action();      /* blocks for the action's lifetime */
            menuResetSurface();  /* the action owned the screen; restore ours */
        }
        else if (item->kind == SETTING_CATEGORY && (s_depth + 1U < SETTINGS_MAX_DEPTH))
        {
            LOGGER_LOG_INFO(LOGGER_MENU, "settings: enter '%s'", item->label);
            s_depth++;
            s_stack[s_depth].node = item;
            s_stack[s_depth].selected = 0U;
            buzzerPlay(0U, false, s_toggle_notes, 2U);
        }
    }

    return MENU_STAY;
}

/* Gap between a slider's gauge and its right-aligned percentage value. */
#define ROW_GAUGE_GAP 8

/* Draw one settings row: cursor, label, and its right-aligned value — a [ON]/[OFF]
 * toggle, a slider gauge + percentage, or a ">" chevron for a category/action. */
static uint16_t drawRow(uint16_t n, const SettingNode *item, int16_t y, bool selected, bool cursor_on)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const uint16_t *label_pal = selected ? g_menu_pal_item_sel : g_menu_pal_item;

    if (selected && cursor_on)
    {
        menuDrawText(&font8x8, (int16_t)(ROW_LABEL_X - ROW_CURSOR_DX), y, g_menu_pal_accent, ">");
    }
    menuDrawText(&font8x8, ROW_LABEL_X, y, label_pal, item->label);

    if (item->kind == SETTING_TOGGLE)
    {
        const bool on = item->get();
        const char *value = on ? "[ON]" : "[OFF]";
        const int16_t vx = (int16_t)(screen_w - ROW_RIGHT_PAD - (int16_t)menuTextWidth(font8x8.size, value));
        menuDrawText(&font8x8, vx, y, on ? g_menu_pal_accent : g_menu_pal_footer, value);
    }
    else if (item->kind == SETTING_NUMBER)
    {
        const uint8_t val = item->num_get();
        char value[8];
        snprintf(value, sizeof(value), "%u%%", (unsigned)val);
        const int16_t vx = (int16_t)(screen_w - ROW_RIGHT_PAD - (int16_t)menuTextWidth(font8x8.size, value));
        menuDrawText(&font8x8, vx, y, g_menu_pal_accent, value);

        /* Segmented gauge: one cell per step, the first `filled` lit. */
        const uint8_t total = (uint8_t)((item->num_max - item->num_min) / item->num_step + 1U);
        const uint8_t filled = (uint8_t)((val - item->num_min) / item->num_step + 1U);
        const int16_t gx = (int16_t)(vx - ROW_GAUGE_GAP - (int16_t)menuGaugeWidth(total));
        n = menuDrawGauge(n, gx, (int16_t)(y + 2), total, filled);
    }
    else if (item->kind == SETTING_CATEGORY || item->kind == SETTING_ACTION)
    {
        const int16_t vx = (int16_t)(screen_w - ROW_RIGHT_PAD - (int16_t)menuTextWidth(font8x8.size, ">"));
        menuDrawText(&font8x8, vx, y, label_pal, ">");
    }
    return n;
}

void settingsMenuRender(void)
{
    const NavFrame *const frame = &s_stack[s_depth];
    const SettingNode *const category = frame->node;
    const bool cursor_on = menuCursorVisible();
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, category->label);

    for (uint8_t i = 0U; i < category->child_count; i++)
    {
        const int16_t y = (int16_t)(MENU_LIST_TOP + (int)i * MENU_ROW_H);
        n = drawRow(n, &category->children[i], y, (i == frame->selected), cursor_on);
    }

    /* A slider takes left/right; everything else takes A to enter/toggle. */
    const bool number_selected = (category->child_count > 0U) &&
                                 (category->children[frame->selected].kind == SETTING_NUMBER);
    menuDrawFooter(number_selected ? "UP/DOWN move   LEFT/RIGHT adjust   B back"
                                          : "UP/DOWN move   A select   B back");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}
