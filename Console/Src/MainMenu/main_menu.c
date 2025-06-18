#include "main_menu.h"
#include "fonts.h"
#include <stddef.h>
#include "renderer.h"
#include "loader.h"
#include "game_loader.h"
#include "string.h"
#include "joystick.h"
#include "sysclock.h"
#include "usart.h"

#define FONT_PALETTE_IDX 1U
#define FONT_CHAR_PALETTE '0'
#define MAX_STRING_CHARS_DRAWN 16

static uint32_t s_num_binary_files = 0U;
static uint32_t s_current_highlighted_game = 0U;

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

static const uint8_t getFontSize()
{
    return sizeof(font_star_data);
}

static const uint8_t *getFontData(const char font)
{
    char font_converted = font;
    if (font >= 'a' && font <= 'z')
    {
        font_converted = font - 'a' + 'A';
    }
    switch (font_converted)
    {
    case '*':
        return font_star_data;
    case '0':
        return font_0_data;
    case '1':
        return font_1_data;
    case '2':
        return font_2_data;
    case '3':
        return font_3_data;
    case '4':
        return font_4_data;
    case '5':
        return font_5_data;
    case '6':
        return font_6_data;
    case '7':
        return font_7_data;
    case '8':
        return font_8_data;
    case '9':
        return font_9_data;
    case 'A':
        return font_a_data;
    case 'B':
        return font_b_data;
    case 'C':
        return font_c_data;
    case 'D':
        return font_d_data;
    case 'E':
        return font_e_data;
    case 'F':
        return font_f_data;
    case 'G':
        return font_g_data;
    case 'H':
        return font_h_data;
    case 'I':
        return font_i_data;
    case 'J':
        return font_j_data;
    case 'K':
        return font_k_data;
    case 'L':
        return font_l_data;
    case 'M':
        return font_m_data;
    case 'N':
        return font_n_data;
    case 'O':
        return font_o_data;
    case 'P':
        return font_p_data;
    case 'Q':
        return font_q_data;
    case 'R':
        return font_r_data;
    case 'S':
        return font_s_data;
    case 'T':
        return font_t_data;
    case 'U':
        return font_u_data;
    case 'V':
        return font_v_data;
    case 'W':
        return font_w_data;
    case 'X':
        return font_x_data;
    case 'Y':
        return font_y_data;
    case 'Z':
        return font_z_data;
    default:
        return NULL;
    }
}

static const uint8_t *getFontPalette(const char font)
{
    char font_converted = font;
    if (font >= 'a' && font <= 'z')
    {
        font_converted = font - 'a' + 'A';
    }
    switch (font_converted)
    {
    case '*':
        return font_star_palette;
    case '0':
        return font_0_palette;
    case '1':
        return font_1_palette;
    case '2':
        return font_2_palette;
    case '3':
        return font_3_palette;
    case '4':
        return font_4_palette;
    case '5':
        return font_5_palette;
    case '6':
        return font_6_palette;
    case '7':
        return font_7_palette;
    case '8':
        return font_8_palette;
    case '9':
        return font_9_palette;
    case 'A':
        return font_a_palette;
    case 'B':
        return font_b_palette;
    case 'C':
        return font_c_palette;
    case 'D':
        return font_d_palette;
    case 'E':
        return font_e_palette;
    case 'F':
        return font_f_palette;
    case 'G':
        return font_g_palette;
    case 'H':
        return font_h_palette;
    case 'I':
        return font_i_palette;
    case 'J':
        return font_j_palette;
    case 'K':
        return font_k_palette;
    case 'L':
        return font_l_palette;
    case 'M':
        return font_m_palette;
    case 'N':
        return font_n_palette;
    case 'O':
        return font_o_palette;
    case 'P':
        return font_p_palette;
    case 'Q':
        return font_q_palette;
    case 'R':
        return font_r_palette;
    case 'S':
        return font_s_palette;
    case 'T':
        return font_t_palette;
    case 'U':
        return font_u_palette;
    case 'V':
        return font_v_palette;
    case 'W':
        return font_w_palette;
    case 'X':
        return font_x_palette;
    case 'Y':
        return font_y_palette;
    case 'Z':
        return font_z_palette;
    default:
        return NULL;
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
    rendererPatternTableSetTile(TILE_FONT_STAR, getFontData('*'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_A, getFontData('a'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_B, getFontData('b'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_C, getFontData('c'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_D, getFontData('d'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_E, getFontData('e'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_F, getFontData('f'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_G, getFontData('g'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_H, getFontData('h'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_I, getFontData('i'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_J, getFontData('j'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_K, getFontData('k'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_L, getFontData('l'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_M, getFontData('m'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_N, getFontData('n'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_O, getFontData('o'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_P, getFontData('p'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_Q, getFontData('q'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_R, getFontData('r'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_S, getFontData('s'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_T, getFontData('t'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_U, getFontData('u'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_V, getFontData('v'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_W, getFontData('w'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_X, getFontData('x'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_Y, getFontData('y'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_Z, getFontData('z'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_0, getFontData('0'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_1, getFontData('1'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_2, getFontData('2'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_3, getFontData('3'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_4, getFontData('4'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_5, getFontData('5'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_6, getFontData('6'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_7, getFontData('7'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_8, getFontData('8'), getFontSize());
    rendererPatternTableSetTile(TILE_FONT_9, getFontData('9'), getFontSize());
}

static void fillFramePalette()
{
    rendererFramePaletteSetBackgroundMultiple(FONT_PALETTE_IDX, getFontPalette(FONT_CHAR_PALETTE)[1U], getFontPalette(FONT_CHAR_PALETTE)[2U], getFontPalette(FONT_CHAR_PALETTE)[3U]);
}

void mainMenuInit(void)
{
    rendererInit();
    fillPatternTable();
    fillFramePalette();

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
        }
    }

    if ((joystickGetLBtnUp() || joystickGetRBtnUp()) && (sys_tick_time > (sys_tick_value_up + DEBOUNCE_TIME_MS)))
    {
        sys_tick_value_up = sys_tick_time;
        if (s_current_highlighted_game > 0U)
        {
            s_current_highlighted_game--;
        }
    }

    if (joystickGetSpecialBtn1() && (sys_tick_time > (sys_tick_value_select + DEBOUNCE_TIME_MS)))
    {
        sys_tick_value_select = sys_tick_time;
        if (isGameIndexValid(s_current_highlighted_game))
        {
            debugString("\r\nCurrent s_current_highlighted_game = ");
            debugInt(s_current_highlighted_game);
            gameLoaderLoadGame(s_current_highlighted_game);
            mainMenuInit();
        }
    }
}

static void drawGameSelection(const uint8_t y)
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

void mainMenuUpdate(void)
{
    computeSelectedGame();
    drawGameSelection(6U);
}