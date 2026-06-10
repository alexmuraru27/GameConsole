#include "level_manager.h"
#include "game_console_api.h"
#include "assets.h"
#include "tic_tac_toe_logic.h"
#include "sound.h"

DECLARE_API_HEADER_PTR(s_api_ptr);

static const uint8_t S_CHOOSE_SYMBOL_TEXT_X_POSITION = 5U;
static const uint8_t S_X_SYMBOL_X_TILE_POSITION = 5U;
static const uint8_t S_O_SYMBOL_X_TILE_POSITION = 9U;
static const uint8_t S_SYMBOL_Y_TILE_POSITION = 5U;
static const uint32_t S_BUTTON_DEBOUNCE_TIME_MS = 500U;
static const uint8_t S_BG_GRID_TILE_OFFSET_X = 5U;
static const uint8_t S_BG_GRID_TILE_OFFSET_Y = 4U;
static const uint8_t S_MAX_SELECTED_GRID = 3U;
static const uint8_t S_GAME_METATILE_SIZE = 2U;

// End screen
static const uint8_t S_END_TEXT_OFFSET_Y = 11U;

static uint8_t s_board[3][3];
static uint8_t s_game_state;
static bool s_is_player_symbol_x;
static bool s_is_selection_button_pressed;
static uint8_t s_current_selected_grid_x = 1U;
static uint8_t s_current_selected_grid_y = 1U;
static uint32_t s_last_pressed_time = 0U;
static bool s_is_user_input_disabled = true;

void levelManagerInit()
{
    s_api_ptr->api.rendererOamClear();
    s_api_ptr->api.rendererAttributeTableClear();
    s_api_ptr->api.rendererNameTableClear();
    ticTacToeInitBoard(s_board);
    s_game_state = TIC_TAC_TOE_GAME_STATE_CONTINUE;
    s_is_player_symbol_x = true;
    s_current_selected_grid_x = 1U;
    s_current_selected_grid_y = 1U;
    s_last_pressed_time = 0U;
    s_is_selection_button_pressed = false;
}

static void setTileAndPalette(const uint8_t x,
                              const uint8_t y,
                              const uint8_t tile_idx,
                              const uint8_t palette_bg_idx,
                              const bool is_flip_horiz,
                              const bool is_flip_vert)
{
    s_api_ptr->api.rendererNameTableSetTile(x, y, tile_idx);
    s_api_ptr->api.rendererAttributeTableSetPalette(x, y, palette_bg_idx);

    s_api_ptr->api.rendererAttributeTableSetFlipH(x, y, is_flip_horiz);
    s_api_ptr->api.rendererAttributeTableSetFlipV(x, y, is_flip_vert);
}

static void setSelectionBox(const uint8_t tile_x, const uint8_t tile_y)
{
    s_api_ptr->api.rendererOamSetTileIdx(1U, ASSET_ID_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetTileIdx(2U, ASSET_ID_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetTileIdx(3U, ASSET_ID_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetTileIdx(4U, ASSET_ID_SPRITE_SELECTION);

    s_api_ptr->api.rendererOamSetPaletteIdx(1U, FRAME_PALETTE_IDX_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetPaletteIdx(2U, FRAME_PALETTE_IDX_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetPaletteIdx(3U, FRAME_PALETTE_IDX_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetPaletteIdx(4U, FRAME_PALETTE_IDX_SPRITE_SELECTION);

    s_api_ptr->api.rendererOamSetXYPos(1U, tile_x * s_api_ptr->api.rendererGetTilePixelSize(), tile_y * s_api_ptr->api.rendererGetTilePixelSize());
    s_api_ptr->api.rendererOamSetXYPos(2U, (tile_x + 1U) * s_api_ptr->api.rendererGetTilePixelSize(), tile_y * s_api_ptr->api.rendererGetTilePixelSize());
    s_api_ptr->api.rendererOamSetXYPos(3U, tile_x * s_api_ptr->api.rendererGetTilePixelSize(), (tile_y + 1U) * s_api_ptr->api.rendererGetTilePixelSize());
    s_api_ptr->api.rendererOamSetXYPos(4U, (tile_x + 1U) * s_api_ptr->api.rendererGetTilePixelSize(), (tile_y + 1U) * s_api_ptr->api.rendererGetTilePixelSize());

    s_api_ptr->api.rendererOamSetFlipH(2U, true);
    s_api_ptr->api.rendererOamSetFlipV(3U, true);
    s_api_ptr->api.rendererOamSetFlipH(4U, true);
    s_api_ptr->api.rendererOamSetFlipV(4U, true);
}

static void drawX(const uint8_t tile_x, const uint8_t tile_y, const uint8_t bg_palette)
{
    setTileAndPalette(tile_x, tile_y, ASSET_ID_SPRITE_X, bg_palette, false, false);
    setTileAndPalette(tile_x + 1U, tile_y, ASSET_ID_SPRITE_X, bg_palette, true, false);
    setTileAndPalette(tile_x, tile_y + 1U, ASSET_ID_SPRITE_X, bg_palette, false, true);
    setTileAndPalette(tile_x + 1U, tile_y + 1U, ASSET_ID_SPRITE_X, bg_palette, true, true);
}

static void drawO(const uint8_t tile_x, const uint8_t tile_y, const uint8_t bg_palette)
{
    setTileAndPalette(tile_x, tile_y, ASSET_ID_SPRITE_O, bg_palette, true, false);
    setTileAndPalette(tile_x + 1U, tile_y, ASSET_ID_SPRITE_O, bg_palette, false, false);
    setTileAndPalette(tile_x, tile_y + 1U, ASSET_ID_SPRITE_O, bg_palette, true, true);
    setTileAndPalette(tile_x + 1U, tile_y + 1U, ASSET_ID_SPRITE_O, bg_palette, false, true);
}

bool levelManagerChooseSymbol(bool is_level_transition)
{
    if (is_level_transition)
    {
        levelManagerInit();

        uint8_t choose_x_offset = S_CHOOSE_SYMBOL_TEXT_X_POSITION;
        uint8_t choose_y_offset = 1U;

        setTileAndPalette(choose_x_offset++, choose_y_offset, ASSET_ID_FONT_C, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(choose_x_offset++, choose_y_offset, ASSET_ID_FONT_H, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(choose_x_offset++, choose_y_offset, ASSET_ID_FONT_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(choose_x_offset++, choose_y_offset, ASSET_ID_FONT_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(choose_x_offset++, choose_y_offset, ASSET_ID_FONT_S, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(choose_x_offset++, choose_y_offset, ASSET_ID_FONT_E, FRAME_PALETTE_IDX_BG_FONT, false, false);

        uint8_t symbol_x_offset = S_CHOOSE_SYMBOL_TEXT_X_POSITION;
        uint8_t symbol_y_offset = 2U;
        setTileAndPalette(symbol_x_offset++, symbol_y_offset, ASSET_ID_FONT_S, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(symbol_x_offset++, symbol_y_offset, ASSET_ID_FONT_Y, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(symbol_x_offset++, symbol_y_offset, ASSET_ID_FONT_M, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(symbol_x_offset++, symbol_y_offset, ASSET_ID_FONT_B, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(symbol_x_offset++, symbol_y_offset, ASSET_ID_FONT_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(symbol_x_offset++, symbol_y_offset, ASSET_ID_FONT_L, FRAME_PALETTE_IDX_BG_FONT, false, false);

        drawX(S_X_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION, FRAME_PALETTE_IDX_BG_FONT);
        drawO(S_O_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION, FRAME_PALETTE_IDX_BG_FONT);
    }

    // joystick L/R
    if ((s_api_ptr->api.joystickGetRBtnLeft() || s_api_ptr->api.joystickGetRBtnRight()) && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
    {
        s_last_pressed_time = s_api_ptr->api.getSysTime();
        s_is_player_symbol_x = !s_is_player_symbol_x;
    }

    setSelectionBox(s_is_player_symbol_x ? S_X_SYMBOL_X_TILE_POSITION : S_O_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION);

    // joystick down selects
    if (s_api_ptr->api.joystickGetSpecialBtn1())
    {
        return true;
    }

    return false;
}

static void handleUserInput()
{
    if (s_is_user_input_disabled)
    {
        if ((!s_api_ptr->api.joystickIsAnyButtonPressed()) && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_is_user_input_disabled = false;
            s_last_pressed_time = s_api_ptr->api.getSysTime();
        }
    }
    else
    {
        if (s_api_ptr->api.joystickGetRBtnLeft() && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_last_pressed_time = s_api_ptr->api.getSysTime();
            if (s_current_selected_grid_x > 0U)
            {
                s_current_selected_grid_x--;
            }
        }

        if (s_api_ptr->api.joystickGetRBtnRight() && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_last_pressed_time = s_api_ptr->api.getSysTime();
            if (s_current_selected_grid_x + 1U < S_MAX_SELECTED_GRID)
            {
                s_current_selected_grid_x++;
            }
        }

        if (s_api_ptr->api.joystickGetRBtnUp() && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_last_pressed_time = s_api_ptr->api.getSysTime();
            if (s_current_selected_grid_y > 0U)
            {
                s_current_selected_grid_y--;
            }
        }

        if (s_api_ptr->api.joystickGetRBtnDown() && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_last_pressed_time = s_api_ptr->api.getSysTime();
            if (s_current_selected_grid_y + 1U < S_MAX_SELECTED_GRID)
            {
                s_current_selected_grid_y++;
            }
        }
        if (s_api_ptr->api.joystickGetSpecialBtn1() && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_is_selection_button_pressed = true;
        }
        else
        {
            s_is_selection_button_pressed = false;
        }
    }
}

static void drawBoard()
{
    for (int x_board = 0U; x_board < 3U; x_board++)
    {
        for (int y_board = 0U; y_board < 3U; y_board++)
        {
            const uint8_t x_coord = S_BG_GRID_TILE_OFFSET_X + (x_board * S_GAME_METATILE_SIZE);
            const uint8_t y_coord = S_BG_GRID_TILE_OFFSET_Y + (y_board * S_GAME_METATILE_SIZE);

            if (s_board[y_board][x_board] == TIC_TAC_TOE_BOARD_PLAYER_X)
            {
                drawX(x_coord, y_coord, FRAME_PALETTE_IDX_BG_FONT);
            }
            if (s_board[y_board][x_board] == TIC_TAC_TOE_BOARD_PLAYER_O)
            {
                drawO(x_coord, y_coord, FRAME_PALETTE_IDX_BG_FONT);
            }
        }
    }
}

// Returns true if game is in progress
bool levelManagerPlay(bool is_level_transition)
{
    if (is_level_transition)
    {
        s_is_user_input_disabled = true;
        s_api_ptr->api.rendererOamClear();
        s_api_ptr->api.rendererAttributeTableClear();
        s_api_ptr->api.rendererNameTableClear();
        // Vertical bars
        // Top
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 2U, S_BG_GRID_TILE_OFFSET_Y, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 2U, S_BG_GRID_TILE_OFFSET_Y + 1U, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 3U, S_BG_GRID_TILE_OFFSET_Y, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, true, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 3U, S_BG_GRID_TILE_OFFSET_Y + 1U, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, true, false);

        // Bottom
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 2U, S_BG_GRID_TILE_OFFSET_Y + 4U, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 2U, S_BG_GRID_TILE_OFFSET_Y + 5U, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 3U, S_BG_GRID_TILE_OFFSET_Y + 4U, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, true, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 3U, S_BG_GRID_TILE_OFFSET_Y + 5U, ASSET_ID_BACKGROUND_V_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, true, false);

        // Horizontal bars
        // left
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X, S_BG_GRID_TILE_OFFSET_Y + 2U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, true);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 1U, S_BG_GRID_TILE_OFFSET_Y + 2U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, true);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X, S_BG_GRID_TILE_OFFSET_Y + 3U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 1U, S_BG_GRID_TILE_OFFSET_Y + 3U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);

        // right
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 4U, S_BG_GRID_TILE_OFFSET_Y + 2U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, true);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 5U, S_BG_GRID_TILE_OFFSET_Y + 2U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, true);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 4U, S_BG_GRID_TILE_OFFSET_Y + 3U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 5U, S_BG_GRID_TILE_OFFSET_Y + 3U, ASSET_ID_BACKGROUND_H_BAR, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);

        // inner square
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 2U, S_BG_GRID_TILE_OFFSET_Y + 2U, ASSET_ID_BACKGROUND_CORNER, FRAME_PALETTE_IDX_BG_BACKGROUND, false, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 3U, S_BG_GRID_TILE_OFFSET_Y + 2U, ASSET_ID_BACKGROUND_CORNER, FRAME_PALETTE_IDX_BG_BACKGROUND, true, false);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 2U, S_BG_GRID_TILE_OFFSET_Y + 3U, ASSET_ID_BACKGROUND_CORNER, FRAME_PALETTE_IDX_BG_BACKGROUND, false, true);
        setTileAndPalette(S_BG_GRID_TILE_OFFSET_X + 3U, S_BG_GRID_TILE_OFFSET_Y + 3U, ASSET_ID_BACKGROUND_CORNER, FRAME_PALETTE_IDX_BG_BACKGROUND, true, true);
    }
    handleUserInput();

    if (s_is_selection_button_pressed)
    {
        uint8_t player_symbol = s_is_player_symbol_x ? TIC_TAC_TOE_BOARD_PLAYER_X : TIC_TAC_TOE_BOARD_PLAYER_O;
        uint8_t ai_symbol = s_is_player_symbol_x ? TIC_TAC_TOE_BOARD_PLAYER_O : TIC_TAC_TOE_BOARD_PLAYER_X;
        if (ticTacToeMakeMove(s_board, s_current_selected_grid_y, s_current_selected_grid_x, player_symbol))
        {
            // if game hasn't finished
            if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_CONTINUE)
            {
                uint8_t ai_row = 0xFF;
                uint8_t ai_col = 0xFF;
                // find best move and apply it
                ticTacToeFindBestMove(s_board, ai_symbol, player_symbol, &ai_row, &ai_col);
                ticTacToeMakeMove(s_board, ai_row, ai_col, ai_symbol);
            }
            drawBoard();
        }
    }

    setSelectionBox(S_BG_GRID_TILE_OFFSET_X + (s_current_selected_grid_x * S_GAME_METATILE_SIZE), S_BG_GRID_TILE_OFFSET_Y + (s_current_selected_grid_y * S_GAME_METATILE_SIZE));

    return (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_CONTINUE);
}

static uint8_t centerStringXOffset(const uint8_t string_length)
{
    if (string_length >= s_api_ptr->api.rendererGetWidthTiles())
    {
        return 0U;
    }
    return (s_api_ptr->api.rendererGetWidthTiles() - string_length) / 2U;
}

bool levelManagerEnd(bool is_level_transition)
{
    if (is_level_transition)
    {
        s_is_user_input_disabled = true;
        s_last_pressed_time = s_api_ptr->api.getSysTime();

        // END Game text
        uint8_t end_text_y_offset = S_END_TEXT_OFFSET_Y;
        if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_WIN_X)
        {
            uint8_t end_text_x_offset = centerStringXOffset(5U);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_X, FRAME_PALETTE_IDX_BG_FONT, false, false);
            end_text_x_offset++;
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_W, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_N, FRAME_PALETTE_IDX_BG_FONT, false, false);
        }
        else if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_WIN_O)
        {
            uint8_t end_text_x_offset = centerStringXOffset(5U);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
            end_text_x_offset++;
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_W, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_N, FRAME_PALETTE_IDX_BG_FONT, false, false);
        }
        else if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_DRAW)
        {
            uint8_t end_text_x_offset = centerStringXOffset(4U);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_D, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_R, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_A, FRAME_PALETTE_IDX_BG_FONT, false, false);
            setTileAndPalette(end_text_x_offset++, end_text_y_offset, ASSET_ID_FONT_W, FRAME_PALETTE_IDX_BG_FONT, false, false);
        }

        // End game sound
        if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_DRAW)
        {
            playSound(ASSET_ID_DRAW_SOUND);
        }
        else if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_WIN_X && s_is_player_symbol_x)
        {
            playSound(ASSET_ID_WIN_SOUND);
        }
        else
        {
            playSound(ASSET_ID_LOOSE_SOUND);
        }
    }

    if (s_is_user_input_disabled)
    {
        if ((!s_api_ptr->api.joystickIsAnyButtonPressed()) && (s_last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
        {
            s_is_user_input_disabled = false;
        }
    }
    return (!s_is_user_input_disabled && s_api_ptr->api.joystickIsAnyButtonPressed());
}