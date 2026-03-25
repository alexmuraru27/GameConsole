#include "main_menu.h"
#include "fonts.h"
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

const uint8_t s_font_star[64U] = {DEFINE_FONT_STAR_TILE};
const uint8_t s_font_a[64U] = {DEFINE_FONT_A_TILE};
const uint8_t s_font_b[64U] = {DEFINE_FONT_B_TILE};
const uint8_t s_font_c[64U] = {DEFINE_FONT_C_TILE};
const uint8_t s_font_d[64U] = {DEFINE_FONT_D_TILE};
const uint8_t s_font_e[64U] = {DEFINE_FONT_E_TILE};
const uint8_t s_font_f[64U] = {DEFINE_FONT_F_TILE};
const uint8_t s_font_g[64U] = {DEFINE_FONT_G_TILE};
const uint8_t s_font_h[64U] = {DEFINE_FONT_H_TILE};
const uint8_t s_font_i[64U] = {DEFINE_FONT_I_TILE};
const uint8_t s_font_j[64U] = {DEFINE_FONT_J_TILE};
const uint8_t s_font_k[64U] = {DEFINE_FONT_K_TILE};
const uint8_t s_font_l[64U] = {DEFINE_FONT_L_TILE};
const uint8_t s_font_m[64U] = {DEFINE_FONT_M_TILE};
const uint8_t s_font_n[64U] = {DEFINE_FONT_N_TILE};
const uint8_t s_font_o[64U] = {DEFINE_FONT_O_TILE};
const uint8_t s_font_p[64U] = {DEFINE_FONT_P_TILE};
const uint8_t s_font_q[64U] = {DEFINE_FONT_Q_TILE};
const uint8_t s_font_r[64U] = {DEFINE_FONT_R_TILE};
const uint8_t s_font_s[64U] = {DEFINE_FONT_S_TILE};
const uint8_t s_font_t[64U] = {DEFINE_FONT_T_TILE};
const uint8_t s_font_u[64U] = {DEFINE_FONT_U_TILE};
const uint8_t s_font_v[64U] = {DEFINE_FONT_V_TILE};
const uint8_t s_font_w[64U] = {DEFINE_FONT_W_TILE};
const uint8_t s_font_x[64U] = {DEFINE_FONT_X_TILE};
const uint8_t s_font_y[64U] = {DEFINE_FONT_Y_TILE};
const uint8_t s_font_z[64U] = {DEFINE_FONT_Z_TILE};
const uint8_t s_font_0[64U] = {DEFINE_FONT_0_TILE};
const uint8_t s_font_1[64U] = {DEFINE_FONT_1_TILE};
const uint8_t s_font_2[64U] = {DEFINE_FONT_2_TILE};
const uint8_t s_font_3[64U] = {DEFINE_FONT_3_TILE};
const uint8_t s_font_4[64U] = {DEFINE_FONT_4_TILE};
const uint8_t s_font_5[64U] = {DEFINE_FONT_5_TILE};
const uint8_t s_font_6[64U] = {DEFINE_FONT_6_TILE};
const uint8_t s_font_7[64U] = {DEFINE_FONT_7_TILE};
const uint8_t s_font_8[64U] = {DEFINE_FONT_8_TILE};
const uint8_t s_font_9[64U] = {DEFINE_FONT_9_TILE};

typedef enum
{
    TILE_FONT_STAR = 1U,
    TILE_FONT_A,
    TILE_FONT_B,
    TILE_FONT_C,
    TILE_FONT_D,
    TILE_FONT_E,
    TILE_FONT_F,
    TILE_FONT_G,
    TILE_FONT_H,
    TILE_FONT_I,
    TILE_FONT_J,
    TILE_FONT_K,
    TILE_FONT_L,
    TILE_FONT_M,
    TILE_FONT_N,
    TILE_FONT_O,
    TILE_FONT_P,
    TILE_FONT_Q,
    TILE_FONT_R,
    TILE_FONT_S,
    TILE_FONT_T,
    TILE_FONT_U,
    TILE_FONT_V,
    TILE_FONT_W,
    TILE_FONT_X,
    TILE_FONT_Y,
    TILE_FONT_Z,
    TILE_FONT_0,
    TILE_FONT_1,
    TILE_FONT_2,
    TILE_FONT_3,
    TILE_FONT_4,
    TILE_FONT_5,
    TILE_FONT_6,
    TILE_FONT_7,
    TILE_FONT_8,
    TILE_FONT_9,
    TILE_FONT_INVALID = 255U,
} TILES;

static TILES charToTile(char font)
{
    char font_converted = font;
    if (font >= 'a' && font <= 'z')
    {
        font_converted = font - 'a' + 'A';
    }
    switch (font_converted)
    {
    case '*':
        return TILE_FONT_STAR;
    case '0':
        return TILE_FONT_0;
    case '1':
        return TILE_FONT_1;
    case '2':
        return TILE_FONT_2;
    case '3':
        return TILE_FONT_3;
    case '4':
        return TILE_FONT_4;
    case '5':
        return TILE_FONT_5;
    case '6':
        return TILE_FONT_6;
    case '7':
        return TILE_FONT_7;
    case '8':
        return TILE_FONT_8;
    case '9':
        return TILE_FONT_9;
    case 'A':
        return TILE_FONT_A;
    case 'B':
        return TILE_FONT_B;
    case 'C':
        return TILE_FONT_C;
    case 'D':
        return TILE_FONT_D;
    case 'E':
        return TILE_FONT_E;
    case 'F':
        return TILE_FONT_F;
    case 'G':
        return TILE_FONT_G;
    case 'H':
        return TILE_FONT_H;
    case 'I':
        return TILE_FONT_I;
    case 'J':
        return TILE_FONT_J;
    case 'K':
        return TILE_FONT_K;
    case 'L':
        return TILE_FONT_L;
    case 'M':
        return TILE_FONT_M;
    case 'N':
        return TILE_FONT_N;
    case 'O':
        return TILE_FONT_O;
    case 'P':
        return TILE_FONT_P;
    case 'Q':
        return TILE_FONT_Q;
    case 'R':
        return TILE_FONT_R;
    case 'S':
        return TILE_FONT_S;
    case 'T':
        return TILE_FONT_T;
    case 'U':
        return TILE_FONT_U;
    case 'V':
        return TILE_FONT_V;
    case 'W':
        return TILE_FONT_W;
    case 'X':
        return TILE_FONT_X;
    case 'Y':
        return TILE_FONT_Y;
    case 'Z':
        return TILE_FONT_Z;
    default:
        return TILE_FONT_INVALID;
    }
}

static uint8_t centerStringXOffset(const char *string)
{
    const uint8_t string_length = strlen(string);
    if (string_length >= rendererGetWidthTiles())
    {
        return 0U;
    }
    return (rendererGetWidthTiles() - strlen(string)) / 2U;
}

static void drawLetter(uint8_t x, uint8_t y, char letter, uint8_t palette_idx)
{
    if (x < rendererGetWidthTiles() && y < rendererGetHeightTiles())
    {
        rendererNameTableSetTile(x, y, charToTile(letter));
        rendererAttributeTableSetPalette(x, y, palette_idx);
    }
}

static void drawClearPartialLine(uint8_t x, uint8_t y, uint8_t length)
{
    if (x < rendererGetWidthTiles() && y < rendererGetHeightTiles())
    {
        for (uint8_t idx_x = x; idx_x < x + length; ++idx_x)
        {
            if (idx_x < rendererGetWidthTiles())
            {
                rendererNameTableSetTile(idx_x, y, 0U);
                rendererAttributeTableSetPalette(idx_x, y, 0U);
            }
            else
            {
                break;
            }
        }
    }
}

static void drawString(uint8_t x, uint8_t y, const char *str)
{
    uint8_t x_offset = x;
    uint8_t str_idx = 0U;
    while (str_idx < MAX_STRING_CHARS_DRAWN && str[str_idx] != '\0')
    {
        if (charToTile(str[str_idx]) != TILE_FONT_INVALID)
        {
            drawLetter(x_offset, y, str[str_idx], FONT_PALETTE_IDX);
        }
        str_idx++;
        x_offset++;
    }
}

static void fillPatternTable()
{
    rendererPatternTableSetTile(TILE_FONT_STAR, s_font_star, 64U);
    rendererPatternTableSetTile(TILE_FONT_A, s_font_a, 64U);
    rendererPatternTableSetTile(TILE_FONT_B, s_font_b, 64U);
    rendererPatternTableSetTile(TILE_FONT_C, s_font_c, 64U);
    rendererPatternTableSetTile(TILE_FONT_D, s_font_d, 64U);
    rendererPatternTableSetTile(TILE_FONT_E, s_font_e, 64U);
    rendererPatternTableSetTile(TILE_FONT_F, s_font_f, 64U);
    rendererPatternTableSetTile(TILE_FONT_G, s_font_g, 64U);
    rendererPatternTableSetTile(TILE_FONT_H, s_font_h, 64U);
    rendererPatternTableSetTile(TILE_FONT_I, s_font_i, 64U);
    rendererPatternTableSetTile(TILE_FONT_J, s_font_j, 64U);
    rendererPatternTableSetTile(TILE_FONT_K, s_font_k, 64U);
    rendererPatternTableSetTile(TILE_FONT_L, s_font_l, 64U);
    rendererPatternTableSetTile(TILE_FONT_M, s_font_m, 64U);
    rendererPatternTableSetTile(TILE_FONT_N, s_font_n, 64U);
    rendererPatternTableSetTile(TILE_FONT_O, s_font_o, 64U);
    rendererPatternTableSetTile(TILE_FONT_P, s_font_p, 64U);
    rendererPatternTableSetTile(TILE_FONT_Q, s_font_q, 64U);
    rendererPatternTableSetTile(TILE_FONT_R, s_font_r, 64U);
    rendererPatternTableSetTile(TILE_FONT_S, s_font_s, 64U);
    rendererPatternTableSetTile(TILE_FONT_T, s_font_t, 64U);
    rendererPatternTableSetTile(TILE_FONT_U, s_font_u, 64U);
    rendererPatternTableSetTile(TILE_FONT_V, s_font_v, 64U);
    rendererPatternTableSetTile(TILE_FONT_W, s_font_w, 64U);
    rendererPatternTableSetTile(TILE_FONT_X, s_font_x, 64U);
    rendererPatternTableSetTile(TILE_FONT_Y, s_font_y, 64U);
    rendererPatternTableSetTile(TILE_FONT_Z, s_font_z, 64U);
    rendererPatternTableSetTile(TILE_FONT_0, s_font_0, 64U);
    rendererPatternTableSetTile(TILE_FONT_1, s_font_1, 64U);
    rendererPatternTableSetTile(TILE_FONT_2, s_font_2, 64U);
    rendererPatternTableSetTile(TILE_FONT_3, s_font_3, 64U);
    rendererPatternTableSetTile(TILE_FONT_4, s_font_4, 64U);
    rendererPatternTableSetTile(TILE_FONT_5, s_font_5, 64U);
    rendererPatternTableSetTile(TILE_FONT_6, s_font_6, 64U);
    rendererPatternTableSetTile(TILE_FONT_7, s_font_7, 64U);
    rendererPatternTableSetTile(TILE_FONT_8, s_font_8, 64U);
    rendererPatternTableSetTile(TILE_FONT_9, s_font_9, 64U);
}

static void fillFramePalette()
{
    uint8_t palettes[4U] = {DEFINE_FONT_A_PALETTE};
    rendererFramePaletteSetBackgroundMultiple(FONT_PALETTE_IDX, palettes[1U], palettes[2U], palettes[3U]);
}

void mainMenuInit(void)
{
    rendererInit();
    fillPatternTable();
    fillFramePalette();

    s_is_to_redraw_game_menu = true;
    s_num_binary_files = loaderGetBinaryFilesNumberInDirectory();
    s_current_highlighted_game = 0U;

    const char *welcome_string = "*HELLO PLAYER*";
    drawString(centerStringXOffset(welcome_string), 1U, welcome_string);

    const char *choose_string = "Choose to play";
    drawString(centerStringXOffset(choose_string), 2U, choose_string);
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

static void drawGameSelection(const uint8_t y)
{
    if (s_is_to_redraw_game_menu)
    {
        s_is_to_redraw_game_menu = false;

        if (s_num_binary_files > 0U)
        {
            const uint8_t x_offset = 1U;
            uint32_t filename_length = 0U;
            char filename_out[loaderGetMaxFilenameSize()];

            drawString(x_offset - 1U, y, "*");

            for (uint32_t i = 0U; i < 3U; i++)
            {
                const uint32_t game_idx = s_current_highlighted_game + i;
                drawClearPartialLine(x_offset, y + i, rendererGetWidthTiles());
                if (isGameIndexValid(game_idx))
                {
                    if ((loaderGetFilenameByIndex(game_idx, filename_out, &filename_length) == FR_OK) && (filename_length > 1U))
                    {
                        drawString(x_offset, y + i, filename_out);
                    }
                }
            }
        }
        else
        {
            drawString(1U, y, "No game found");
        }
    }
}

void mainMenuUpdate(void)
{
    computeSelectedGame();
    drawGameSelection(6U);
}