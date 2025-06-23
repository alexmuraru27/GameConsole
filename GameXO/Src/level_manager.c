#include "level_manager.h"
#include "game_console_api.h"
#include "assets.h"
DECLARE_API_HEADER_PTR(s_api_ptr);

static const uint8_t S_CHOOSE_SYMBOL_TEXT_X_POSITION = 5U;
static const uint8_t S_X_SYMBOL_X_TILE_POSITION = 5U;
static const uint8_t S_O_SYMBOL_X_TILE_POSITION = 9U;
static const uint8_t S_SYMBOL_Y_TILE_POSITION = 5U;

void levelManagerInit()
{
    s_api_ptr->api.rendererOamClear();
    s_api_ptr->api.rendererAttributeTableClear();
    s_api_ptr->api.rendererNameTableClear();
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

static void setSelectionBox(const bool is_player_x)
{
    s_api_ptr->api.rendererOamSetTileIdx(1U, ASSET_ID_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetTileIdx(2U, ASSET_ID_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetTileIdx(3U, ASSET_ID_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetTileIdx(4U, ASSET_ID_SPRITE_SELECTION);

    s_api_ptr->api.rendererOamSetPaletteIdx(1U, FRAME_PALETTE_IDX_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetPaletteIdx(2U, FRAME_PALETTE_IDX_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetPaletteIdx(3U, FRAME_PALETTE_IDX_SPRITE_SELECTION);
    s_api_ptr->api.rendererOamSetPaletteIdx(4U, FRAME_PALETTE_IDX_SPRITE_SELECTION);

    uint8_t selection_start_pos_x = is_player_x ? S_X_SYMBOL_X_TILE_POSITION : S_O_SYMBOL_X_TILE_POSITION;
    uint8_t selection_start_pos_y = S_SYMBOL_Y_TILE_POSITION;
    s_api_ptr->api.rendererOamSetXYPos(1U, selection_start_pos_x * s_api_ptr->api.rendererGetTilePixelSize(), selection_start_pos_y * s_api_ptr->api.rendererGetTilePixelSize());
    s_api_ptr->api.rendererOamSetXYPos(2U, (selection_start_pos_x + 1U) * s_api_ptr->api.rendererGetTilePixelSize(), selection_start_pos_y * s_api_ptr->api.rendererGetTilePixelSize());
    s_api_ptr->api.rendererOamSetXYPos(3U, selection_start_pos_x * s_api_ptr->api.rendererGetTilePixelSize(), (selection_start_pos_y + 1U) * s_api_ptr->api.rendererGetTilePixelSize());
    s_api_ptr->api.rendererOamSetXYPos(4U, (selection_start_pos_x + 1U) * s_api_ptr->api.rendererGetTilePixelSize(), (selection_start_pos_y + 1U) * s_api_ptr->api.rendererGetTilePixelSize());

    s_api_ptr->api.rendererOamSetFlipH(2U, true);
    s_api_ptr->api.rendererOamSetFlipV(3U, true);
    s_api_ptr->api.rendererOamSetFlipH(4U, true);
    s_api_ptr->api.rendererOamSetFlipV(4U, true);
}

void levelManagerChooseSymbol(bool is_level_transition, bool is_player_x)
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

        setTileAndPalette(S_X_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION, ASSET_ID_SPRITE_X, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(S_X_SYMBOL_X_TILE_POSITION + 1U, S_SYMBOL_Y_TILE_POSITION, ASSET_ID_SPRITE_X, FRAME_PALETTE_IDX_BG_FONT, true, false);
        setTileAndPalette(S_X_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION + 1U, ASSET_ID_SPRITE_X, FRAME_PALETTE_IDX_BG_FONT, false, true);
        setTileAndPalette(S_X_SYMBOL_X_TILE_POSITION + 1U, S_SYMBOL_Y_TILE_POSITION + 1U, ASSET_ID_SPRITE_X, FRAME_PALETTE_IDX_BG_FONT, true, true);

        setTileAndPalette(S_O_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION, ASSET_ID_SPRITE_O, FRAME_PALETTE_IDX_BG_FONT, true, false);
        setTileAndPalette(S_O_SYMBOL_X_TILE_POSITION + 1U, S_SYMBOL_Y_TILE_POSITION, ASSET_ID_SPRITE_O, FRAME_PALETTE_IDX_BG_FONT, false, false);
        setTileAndPalette(S_O_SYMBOL_X_TILE_POSITION, S_SYMBOL_Y_TILE_POSITION + 1U, ASSET_ID_SPRITE_O, FRAME_PALETTE_IDX_BG_FONT, true, true);
        setTileAndPalette(S_O_SYMBOL_X_TILE_POSITION + 1U, S_SYMBOL_Y_TILE_POSITION + 1U, ASSET_ID_SPRITE_O, FRAME_PALETTE_IDX_BG_FONT, false, true);
    }

    setSelectionBox(is_player_x);
}

// Returns true if game is in progress
bool levelManagerPlay(bool is_level_transition, bool is_player_x)
{
    bool is_game_in_progress = true;
    return is_game_in_progress;
}

void levelManagerEnd(bool is_level_transition, bool is_player_x)
{
}