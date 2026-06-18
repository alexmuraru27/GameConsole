#include "settings_menu.h"
#include "renderer.h"
#include "buzzer.h"
#include "console_settings_storage.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"

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
    /* future: SETTING_TEXT, SETTING_NUMBER, SETTING_ACTION */
} SettingKind;

typedef struct SettingNode
{
    const char *label;
    SettingKind kind;
    bool (*get)(void);       /* TOGGLE: read the current value */
    void (*set)(bool value); /* TOGGLE: apply + persist the new value */
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

/* ---- The tree ---- */

static const SettingNode s_root_children[] = {
    {.label = "Buzzer Sound", .kind = SETTING_TOGGLE, .get = buzzerSoundGet, .set = buzzerSoundSet},
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
#define ROW_CURSOR_DX 18 /* cursor sits this far left of the label */
#define ROW_RIGHT_PAD 60 /* right margin for a toggle's value column */

static const uint16_t s_move_notes[] = {NOTE_A5, 24U};
static const uint16_t s_toggle_notes[] = {NOTE_E5, 40U, NOTE_A5, 60U};

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
            return MENU_GOTO_ROOT;
        }
        s_depth--;
        return MENU_STAY;
    }

    if (category->child_count == 0U)
    {
        return MENU_STAY;
    }

    if (nav.down && (frame->selected + 1U < category->child_count))
    {
        frame->selected++;
        buzzerPlay(0U, false, s_move_notes, 1U);
    }
    else if (nav.up && (frame->selected > 0U))
    {
        frame->selected--;
        buzzerPlay(0U, false, s_move_notes, 1U);
    }
    else if (nav.enter)
    {
        const SettingNode *const item = &category->children[frame->selected];
        if (item->kind == SETTING_TOGGLE)
        {
            item->set(!item->get());
            buzzerPlay(0U, false, s_toggle_notes, 2U);
        }
        else if (item->kind == SETTING_CATEGORY && (s_depth + 1U < SETTINGS_MAX_DEPTH))
        {
            s_depth++;
            s_stack[s_depth].node = item;
            s_stack[s_depth].selected = 0U;
            buzzerPlay(0U, false, s_toggle_notes, 2U);
        }
    }

    return MENU_STAY;
}

/* Draw one settings row: cursor, label, and (for a toggle) its right-aligned value. */
static uint16_t drawRow(uint16_t n, const SettingNode *item, int16_t y, bool selected, bool cursor_on)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    const uint16_t *label_pal = selected ? g_menu_pal_item_sel : g_menu_pal_item;

    if (selected && cursor_on)
    {
        n = menuDrawText(n, &font8x8, (int16_t)(ROW_LABEL_X - ROW_CURSOR_DX), y, g_menu_pal_accent, ">");
    }
    n = menuDrawText(n, &font8x8, ROW_LABEL_X, y, label_pal, item->label);

    if (item->kind == SETTING_TOGGLE)
    {
        const bool on = item->get();
        const char *value = on ? "[ON]" : "[OFF]";
        const int16_t vx = (int16_t)(screen_w - ROW_RIGHT_PAD - (int16_t)menuTextWidth(font8x8.size, value));
        n = menuDrawText(n, &font8x8, vx, y, on ? g_menu_pal_accent : g_menu_pal_footer, value);
    }
    else if (item->kind == SETTING_CATEGORY)
    {
        const int16_t vx = (int16_t)(screen_w - ROW_RIGHT_PAD - (int16_t)menuTextWidth(font8x8.size, ">"));
        n = menuDrawText(n, &font8x8, vx, y, label_pal, ">");
    }
    return n;
}

void settingsMenuRender(void)
{
    const NavFrame *const frame = &s_stack[s_depth];
    const SettingNode *const category = frame->node;
    const bool cursor_on = ((getSysTime() / 450U) & 1U) == 0U;
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, category->label);

    for (uint8_t i = 0U; i < category->child_count; i++)
    {
        const int16_t y = (int16_t)(MENU_LIST_TOP + (int)i * MENU_ROW_H);
        n = drawRow(n, &category->children[i], y, (i == frame->selected), cursor_on);
    }

    n = menuDrawFooter(n, "UP/DOWN move   A select   B back");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}
