#include "keyboard.h"

#include <string.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "joystick.h"

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
 * Split-pad input: the RIGHT pad navigates the key grid, the LEFT pad moves the
 * text caret through what's already typed. A single debounced edge per window
 * (across all inputs) keeps it from racing while a direction is held.
 */
typedef struct
{
    bool grid_up, grid_down, grid_left, grid_right; /* right pad */
    bool caret_left, caret_right, caret_home, caret_end; /* left pad */
    bool type;   /* Special Button 1 */
    bool cancel; /* Special Button 2 */
} KbNav;

static KbNav kbPoll(void)
{
    static uint32_t last_action = 0U;
    const uint32_t DEBOUNCE_MS = 180U;
    const uint32_t now = getSysTime();
    KbNav nav = {0};

    if (now <= last_action + DEBOUNCE_MS)
    {
        return nav;
    }

    if (joystickGetRBtnUp())
    {
        nav.grid_up = true;
    }
    else if (joystickGetRBtnDown())
    {
        nav.grid_down = true;
    }
    else if (joystickGetRBtnLeft())
    {
        nav.grid_left = true;
    }
    else if (joystickGetRBtnRight())
    {
        nav.grid_right = true;
    }
    else if (joystickGetLBtnLeft())
    {
        nav.caret_left = true;
    }
    else if (joystickGetLBtnRight())
    {
        nav.caret_right = true;
    }
    else if (joystickGetLBtnUp())
    {
        nav.caret_home = true;
    }
    else if (joystickGetLBtnDown())
    {
        nav.caret_end = true;
    }
    else if (joystickGetSpecialBtn1())
    {
        nav.type = true;
    }
    else if (joystickGetSpecialBtn2())
    {
        nav.cancel = true;
    }

    if (nav.grid_up || nav.grid_down || nav.grid_left || nav.grid_right ||
        nav.caret_left || nav.caret_right || nav.caret_home || nav.caret_end ||
        nav.type || nav.cancel)
    {
        last_action = now;
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
    *n = menuDrawText(*n, &font8x8, x, y, pal, text);
}

static void render(const char *title, const char *text, uint16_t len, uint16_t caret, bool mask, bool shift, int row, int col)
{
    char glyph[2] = {0, 0};
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, title);

    /* Typed text (masked or plain). */
    char display[96];
    uint16_t dn = 0U;
    for (uint16_t i = 0U; i < len && dn < sizeof(display) - 1U; i++)
    {
        display[dn++] = mask ? '*' : text[i];
    }
    display[dn] = '\0';
    n = menuDrawText(n, &font8x8, KB_X0, KB_TEXT_Y, g_menu_pal_item_sel, display);

    /* Blinking caret at its position within the text (left pad moves it). */
    if (((getSysTime() / 400U) & 1U) == 0U)
    {
        char pre[96];
        const uint16_t pn = (caret < dn) ? caret : dn;
        memcpy(pre, display, pn);
        pre[pn] = '\0';
        const int16_t cx = (int16_t)(KB_X0 + menuTextWidth(font8x8.size, pre));
        n = menuDrawText(n, &font8x8, cx, KB_TEXT_Y, g_menu_pal_accent, "|");
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
        n = menuDrawText(n, &font8x8, KB_ACTION_X[a], ay, pal, KB_ACTIONS[a]);
    }

    n = menuDrawFooter(n, "L-pad: cursor   R-pad: keys   A: type   B: cancel");

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

bool keyboardEnter(const char *title, char *out, uint16_t out_size, bool mask)
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
    while (joystickGetSpecialBtn1() || joystickGetSpecialBtn2())
    {
        /* spin until released */
    }

    for (;;)
    {
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

        render(title, out, len, caret, mask, shift, row, col);
    }
}
