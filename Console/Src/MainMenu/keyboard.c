#include "keyboard.h"

#include <string.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "joystick.h"
#include "watchdog.h"

/*
 * Character rows 0..5 plus a bottom action row (index 6). Letter rows (1..3)
 * render and emit upper-case while SHIFT is on. Each cell is placed on a fixed
 * grid so layout doesn't depend on string-spacing internals.
 */
#define KB_TEXT_ROWS 6
#define KB_ACTION_ROW KB_TEXT_ROWS
#define KB_ROWS (KB_TEXT_ROWS + 1)

static const char *const KB_LAYOUT[KB_TEXT_ROWS] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
    "@#$%&*-_+=",
    "!?.,:;/()~",
};

/* Action cells. */
enum
{
    ACT_SHIFT = 0,
    ACT_SPACE,
    ACT_DEL,
    ACT_DONE,
    ACT_COUNT
};
static const char *const KB_ACTIONS[ACT_COUNT] = {"SHIFT", "SPACE", "DEL", "DONE"};
static const int16_t KB_ACTION_X[ACT_COUNT] = {40, 110, 185, 245};

/* Grid geometry. */
#define KB_CELL_W 20
#define KB_CELL_H 18
#define KB_X0 60
#define KB_Y0 84
#define KB_TEXT_Y 64

static const uint16_t s_key_notes[] = {NOTE_A5, 18U};
static const uint16_t s_done_notes[] = {NOTE_E5, 40U, NOTE_A5, 60U};

/*
 * Split input: the RIGHT side (d-pad OR right analog stick) navigates the key
 * grid, the LEFT side (d-pad OR left analog stick) moves the text caret through
 * what's already typed. Each direction reads either the digital button or the
 * matching analog axis, so both input styles work interchangeably. A single
 * debounced edge per window (across all inputs) keeps it from racing while a
 * direction is held — including a stick pushed and held.
 *
 * Analog convention (joystick.c): Positive X = right, Positive Y = up.
 */
typedef struct
{
    bool grid_up, grid_down, grid_left, grid_right; /* right pad / right stick */
    bool caret_left, caret_right, caret_home, caret_end; /* left pad / left stick */
    bool type;   /* Special Button 1 */
    bool cancel; /* Special Button 2 */
} KbNav;

/* Left-stick / right-stick deflection (of +/-512) that counts as a press. */
#define KB_STICK_THRESHOLD 256
#define KB_CARET_BLINK_MS 400U /* text caret blink; distinct from the menu chevron */
#define KB_REPEAT_DELAY_MS 350U
#define KB_REPEAT_RATE_MS 110U

static KbNav kbPoll(void)
{
    enum
    {
        KB_GRID_UP = 1U << 0,
        KB_GRID_DOWN = 1U << 1,
        KB_GRID_LEFT = 1U << 2,
        KB_GRID_RIGHT = 1U << 3,
        KB_CARET_LEFT = 1U << 4,
        KB_CARET_RIGHT = 1U << 5,
        KB_CARET_HOME = 1U << 6,
        KB_CARET_END = 1U << 7,
        KB_TYPE = 1U << 8,
        KB_CANCEL = 1U << 9,
        KB_DIRS = KB_GRID_UP | KB_GRID_DOWN | KB_GRID_LEFT | KB_GRID_RIGHT |
                  KB_CARET_LEFT | KB_CARET_RIGHT | KB_CARET_HOME | KB_CARET_END,
    };
    static uint16_t s_prev = 0U;
    static uint32_t s_repeat_at = 0U;
    const uint32_t now = getSysTime();

    joystickPollFrame();
    InputState in;
    joystickGetState(&in);

    /* Grid movement from the right pad/stick, caret movement from the left. */
    uint16_t cur = 0U;
    if (in.r_up.held || in.right_y > KB_STICK_THRESHOLD)
    {
        cur |= KB_GRID_UP;
    }
    if (in.r_down.held || in.right_y < -KB_STICK_THRESHOLD)
    {
        cur |= KB_GRID_DOWN;
    }
    if (in.r_left.held || in.right_x < -KB_STICK_THRESHOLD)
    {
        cur |= KB_GRID_LEFT;
    }
    if (in.r_right.held || in.right_x > KB_STICK_THRESHOLD)
    {
        cur |= KB_GRID_RIGHT;
    }
    if (in.l_left.held || in.left_x < -KB_STICK_THRESHOLD)
    {
        cur |= KB_CARET_LEFT;
    }
    if (in.l_right.held || in.left_x > KB_STICK_THRESHOLD)
    {
        cur |= KB_CARET_RIGHT;
    }
    if (in.l_up.held || in.left_y > KB_STICK_THRESHOLD)
    {
        cur |= KB_CARET_HOME;
    }
    if (in.l_down.held || in.left_y < -KB_STICK_THRESHOLD)
    {
        cur |= KB_CARET_END;
    }
    if (in.special1.held)
    {
        cur |= KB_TYPE;
    }
    if (in.special2.held)
    {
        cur |= KB_CANCEL;
    }

    /* Edge fires at once; the 8 movement keys auto-repeat while held (type/cancel
     * are edge-only). */
    uint16_t fire = (uint16_t)(cur & ~s_prev);
    if (fire & KB_DIRS)
    {
        s_repeat_at = now + KB_REPEAT_DELAY_MS;
    }
    else if ((cur & KB_DIRS) && (int32_t)(now - s_repeat_at) >= 0)
    {
        fire |= (uint16_t)(cur & KB_DIRS);
        s_repeat_at = now + KB_REPEAT_RATE_MS;
    }
    s_prev = cur;

    KbNav nav = {0};
    if (fire & KB_GRID_UP)
    {
        nav.grid_up = true;
    }
    else if (fire & KB_GRID_DOWN)
    {
        nav.grid_down = true;
    }
    else if (fire & KB_GRID_LEFT)
    {
        nav.grid_left = true;
    }
    else if (fire & KB_GRID_RIGHT)
    {
        nav.grid_right = true;
    }
    else if (fire & KB_CARET_LEFT)
    {
        nav.caret_left = true;
    }
    else if (fire & KB_CARET_RIGHT)
    {
        nav.caret_right = true;
    }
    else if (fire & KB_CARET_HOME)
    {
        nav.caret_home = true;
    }
    else if (fire & KB_CARET_END)
    {
        nav.caret_end = true;
    }
    else if (fire & KB_TYPE)
    {
        nav.type = true;
    }
    else if (fire & KB_CANCEL)
    {
        nav.cancel = true;
    }
    return nav;
}

static int rowLen(int row)
{
    return (row == KB_ACTION_ROW) ? ACT_COUNT : (int)strlen(KB_LAYOUT[row]);
}

static bool isLetterRow(int row)
{
    return row >= 1 && row <= 3;
}

static char applyShift(char c, bool shift)
{
    if (shift && c >= 'a' && c <= 'z')
    {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static void drawCell(uint16_t *n, int16_t x, int16_t y, const char *text, bool selected)
{
    const uint16_t *pal = selected ? g_menu_pal_accent : g_menu_pal_item;
    if (selected)
    {
        /* a small underline marks the active key */
        *n = menuDrawBar(*n, x - 1, (int16_t)(y + 10), (uint16_t)(menuTextWidth(font8x8.size, text) + 2U), g_menu_pal_accent);
    }
    menuDrawText(&font8x8, x, y, pal, text);
}

static void render(const char *title, const char *text, uint16_t len, uint16_t caret, bool shift, int row, int col)
{
    char glyph[2] = {0, 0};
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, title);

    /* Typed text (always shown in clear). */
    char display[96];
    uint16_t dn = 0U;
    for (uint16_t i = 0U; i < len && dn < sizeof(display) - 1U; i++)
    {
        display[dn++] = text[i];
    }
    display[dn] = '\0';
    menuDrawText(&font8x8, KB_X0, KB_TEXT_Y, g_menu_pal_item_sel, display);

    /* Blinking caret at its position within the text (left pad moves it). */
    if (((getSysTime() / KB_CARET_BLINK_MS) & 1U) == 0U)
    {
        char pre[96];
        const uint16_t pn = (caret < dn) ? caret : dn;
        memcpy(pre, display, pn);
        pre[pn] = '\0';
        const int16_t cx = (int16_t)(KB_X0 + menuTextWidth(font8x8.size, pre));
        menuDrawText(&font8x8, cx, KB_TEXT_Y, g_menu_pal_accent, "|");
    }

    /* Character rows. */
    for (int r = 0; r < KB_TEXT_ROWS; r++)
    {
        const int16_t y = (int16_t)(KB_Y0 + r * KB_CELL_H);
        for (int c = 0; c < rowLen(r); c++)
        {
            glyph[0] = applyShift(KB_LAYOUT[r][c], shift && isLetterRow(r));
            drawCell(&n, (int16_t)(KB_X0 + c * KB_CELL_W), y, glyph, r == row && c == col);
        }
    }

    /* Action row. */
    const int16_t ay = (int16_t)(KB_Y0 + KB_ACTION_ROW * KB_CELL_H);
    for (int a = 0; a < ACT_COUNT; a++)
    {
        const bool sel = (row == KB_ACTION_ROW && col == a);
        const bool shift_on = (a == ACT_SHIFT && shift);
        const uint16_t *pal = sel ? g_menu_pal_accent : (shift_on ? g_menu_pal_item_sel : g_menu_pal_item);
        if (sel)
        {
            n = menuDrawBar(n, KB_ACTION_X[a] - 1, (int16_t)(ay + 10),
                            (uint16_t)(menuTextWidth(font8x8.size, KB_ACTIONS[a]) + 2U), g_menu_pal_accent);
        }
        menuDrawText(&font8x8, KB_ACTION_X[a], ay, pal, KB_ACTIONS[a]);
    }

    menuDrawFooter("L: cursor   R: keys   A: type   B: cancel");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

/* Insert `c` at the caret. */
static void insertChar(char *out, uint16_t out_size, uint16_t *len, uint16_t *caret, char c)
{
    if (*len >= out_size - 1U)
    {
        return;
    }
    memmove(&out[*caret + 1U], &out[*caret], (size_t)(*len - *caret));
    out[*caret] = c;
    (*len)++;
    (*caret)++;
    out[*len] = '\0';
}

/* Backspace: delete the char before the caret. */
static void deleteChar(char *out, uint16_t *len, uint16_t *caret)
{
    if (*caret == 0U)
    {
        return;
    }
    memmove(&out[*caret - 1U], &out[*caret], (size_t)(*len - *caret));
    (*len)--;
    (*caret)--;
    out[*len] = '\0';
}

/* The raw keyboard entry loop — private. Callers go through keyboardModal, which
 * wraps this with the full-screen screen management (backdrop + background
 * save/restore). Blocks until the player confirms (DONE) or cancels. */
static bool keyboardEnter(const char *title, char *out, uint16_t out_size)
{
    if (out == NULL || out_size == 0U)
    {
        return false;
    }
    /* Seed from whatever the caller pre-filled (empty for a fresh entry, the
     * current value when editing). */
    out[out_size - 1U] = '\0';
    uint16_t len = (uint16_t)strnlen(out, out_size - 1U);
    uint16_t caret = len; /* start at the end of any pre-filled text */
    int row = 1, col = 0;
    bool shift = false;

    /* Wait for the button that opened the keyboard to be released, so that press
     * doesn't immediately register as a keystroke (it would type the default
     * highlighted key). */
    for (;;)
    {
        joystickPollFrame();
        InputState in;
        joystickGetState(&in);
        if (!in.special1.held && !in.special2.held)
        {
            break;
        }
    }

    for (;;)
    {
        /* This loop blocks the caller (a menu, or a game via osTextInput) for as
         * long as the player takes to type — well past the IWDG window. It is
         * cooperative waiting, not a wedge, so feed the watchdog like the other
         * long-running blocking loops (downloader, flasher) do. */
        watchdogKick();

        const KbNav nav = kbPoll();

        if (nav.cancel)
        {
            return false;
        }

        /* Right pad: move around the key grid. */
        if (nav.grid_up)
        {
            row = (row > 0) ? row - 1 : 0;
        }
        else if (nav.grid_down)
        {
            row = (row < KB_ROWS - 1) ? row + 1 : KB_ROWS - 1;
        }
        else if (nav.grid_left)
        {
            col = (col > 0) ? col - 1 : 0;
        }
        else if (nav.grid_right)
        {
            col = (col < rowLen(row) - 1) ? col + 1 : col;
        }
        if (col > rowLen(row) - 1)
        {
            col = rowLen(row) - 1; /* clamp after a row change */
        }

        /* Left pad: move the text caret. */
        if (nav.caret_left && caret > 0U)
        {
            caret--;
        }
        else if (nav.caret_right && caret < len)
        {
            caret++;
        }
        else if (nav.caret_home)
        {
            caret = 0U;
        }
        else if (nav.caret_end)
        {
            caret = len;
        }

        if (nav.type)
        {
            if (row == KB_ACTION_ROW)
            {
                switch (col)
                {
                case ACT_SHIFT:
                    shift = !shift;
                    break;
                case ACT_SPACE:
                    insertChar(out, out_size, &len, &caret, ' ');
                    break;
                case ACT_DEL:
                    deleteChar(out, &len, &caret);
                    break;
                case ACT_DONE:
                    buzzerPlay(0U, false, s_done_notes, 2U);
                    return true;
                default:
                    break;
                }
            }
            else
            {
                insertChar(out, out_size, &len, &caret, applyShift(KB_LAYOUT[row][col], shift && isLetterRow(row)));
            }
            buzzerPlay(0U, false, s_key_notes, 1U);
        }

        render(title, out, len, caret, shift, row, col);
    }
}

bool keyboardModal(const char *title, char *out, uint16_t out_size)
{
    /* The single public entry to the on-screen keyboard: a full-screen modal over
     * whatever the caller had on screen — a settings menu, or a running game via
     * osTextInput. Snapshot the renderer background, paint the standard menu
     * backdrop behind the keys, run the keyboard, then hand the background back
     * exactly as it was. A game sets its background once at init and never again,
     * so the modal must leave it untouched (a menu's is already the menu backdrop,
     * so the save/restore is a harmless identity there). */
    bool prev_bg_enabled;
    uint16_t prev_bg_color;
    rendererGetBackground(&prev_bg_enabled, &prev_bg_color);
    menuResetSurface(); /* opaque menu backdrop while the keyboard is open */

    const bool confirmed = keyboardEnter(title, out, out_size);

    rendererSetBackgroundState(prev_bg_enabled, prev_bg_color);
    /* Drop the keyboard's UI layer so its sprites can't bleed into the caller's
     * next frame; the caller (menu or game) rebuilds the screen on its next render. */
    rendererClear();
    return confirmed;
}
