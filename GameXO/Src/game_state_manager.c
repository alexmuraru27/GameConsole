#include "game_state_manager.h"
#include "game_console_api.h"
#include "assets.h"
#include "level_manager.h"

typedef enum
{
    GAME_STATE_INIT,
    GAME_STATE_CHOOSE_SYMBOL,
    GAME_STATE_PLAYING,
    GAME_STATE_END
} GameState;

DECLARE_API_HEADER_PTR(s_api_ptr);
static GameState s_game_state = {GAME_STATE_INIT};
static bool s_game_state_player_is_x = {true};
static bool s_is_state_transition = {true};
static const uint32_t S_BUTTON_DEBOUNCE_TIME_MS = 500U;

static void fillPatternTable()
{
    const uint8_t asset_ids[] = {ASSET_ID_FONT_A,
                                 ASSET_ID_FONT_B,
                                 ASSET_ID_FONT_C,
                                 ASSET_ID_FONT_D,
                                 ASSET_ID_FONT_E,
                                 ASSET_ID_FONT_F,
                                 ASSET_ID_FONT_G,
                                 ASSET_ID_FONT_H,
                                 ASSET_ID_FONT_I,
                                 ASSET_ID_FONT_J,
                                 ASSET_ID_FONT_K,
                                 ASSET_ID_FONT_L,
                                 ASSET_ID_FONT_M,
                                 ASSET_ID_FONT_N,
                                 ASSET_ID_FONT_O,
                                 ASSET_ID_FONT_P,
                                 ASSET_ID_FONT_Q,
                                 ASSET_ID_FONT_R,
                                 ASSET_ID_FONT_S,
                                 ASSET_ID_FONT_T,
                                 ASSET_ID_FONT_U,
                                 ASSET_ID_FONT_V,
                                 ASSET_ID_FONT_W,
                                 ASSET_ID_FONT_X,
                                 ASSET_ID_FONT_Y,
                                 ASSET_ID_FONT_Z,
                                 ASSET_ID_FONT_0,
                                 ASSET_ID_FONT_1,
                                 ASSET_ID_FONT_2,
                                 ASSET_ID_FONT_3,
                                 ASSET_ID_FONT_4,
                                 ASSET_ID_FONT_5,
                                 ASSET_ID_FONT_6,
                                 ASSET_ID_FONT_7,
                                 ASSET_ID_FONT_8,
                                 ASSET_ID_FONT_9,
                                 ASSET_ID_FONT_STAR,
                                 ASSET_ID_SPRITE_X,
                                 ASSET_ID_SPRITE_O,
                                 ASSET_ID_SPRITE_SELECTION};

    uint8_t tile_buffer[64U];
    uint32_t res = 0U;
    for (uint16_t idx = 0U; idx < sizeof(asset_ids) / sizeof(uint8_t); idx++)
    {
        res = s_api_ptr->api.assetLoaderGetAssetData(asset_ids[idx], tile_buffer, sizeof(tile_buffer) / sizeof(uint8_t));
        if (!res)
        {
            s_api_ptr->api.rendererPatternTableSetTile(asset_ids[idx], tile_buffer, 64U);
        }
    }
}

static void fillFramePalette()
{
    // WHITE
    s_api_ptr->api.rendererFramePaletteSetBackgroundMultiple(FRAME_PALETTE_IDX_BG_FONT, 0x20, 0x20, 0x20);

    // GRAY
    s_api_ptr->api.rendererFramePaletteSetBackgroundMultiple(FRAME_PALETTE_IDX_BG_BACKGROUND, 0x2D, 0x2D, 0x2D);

    // GREEN
    s_api_ptr->api.rendererFramePaletteSetSpriteMultiple(FRAME_PALETTE_IDX_SPRITE_SELECTION, 0x1B, 0x1B, 0x1B);

    // BLUE
    s_api_ptr->api.rendererFramePaletteSetSpriteMultiple(FRAME_PALETTE_IDX_SPRITE_X, 0x02, 0x02, 0x02);

    // BROWN
    s_api_ptr->api.rendererFramePaletteSetSpriteMultiple(FRAME_PALETTE_IDX_SPRITE_O, 0x06, 0x06, 0x06);
}

void gameStateManagerInit(void)
{
    s_api_ptr->api.rendererInit();
    s_game_state = GAME_STATE_INIT;

    fillPatternTable();
    fillFramePalette();

    s_api_ptr->api.rendererNameTableSetTile(5U, 6U, ASSET_ID_FONT_A);
    s_api_ptr->api.rendererAttributeTableSetPalette(5U, 6U, FRAME_PALETTE_IDX_BG_FONT);
}

static void handleStateInit()
{
    s_game_state_player_is_x = true;
    s_is_state_transition = true;
    s_game_state = GAME_STATE_CHOOSE_SYMBOL;
    levelManagerInit();
}

static void handleStateChooseSymbol()
{
    static uint32_t last_pressed_time = 0U;
    levelManagerChooseSymbol(s_is_state_transition, s_game_state_player_is_x);
    if (s_is_state_transition)
    {
        s_is_state_transition = false;
    }

    // joystick L/R
    if ((s_api_ptr->api.joystickGetRBtnLeft() || s_api_ptr->api.joystickGetRBtnRight()) && (last_pressed_time + S_BUTTON_DEBOUNCE_TIME_MS < s_api_ptr->api.getSysTime()))
    {
        last_pressed_time = s_api_ptr->api.getSysTime();
        s_game_state_player_is_x = !s_game_state_player_is_x;
    }

    // joystick down selects
    if (s_api_ptr->api.joystickGetRBtnDown())
    {
        s_game_state = GAME_STATE_PLAYING;
        s_is_state_transition = true;
    }
}

static void handleStatePlaying()
{
    const bool is_game_in_progress = levelManagerPlay(s_is_state_transition, s_game_state_player_is_x);

    if (s_is_state_transition)
    {
        s_is_state_transition = false;
    }

    if (!is_game_in_progress)
    {
        s_game_state = GAME_STATE_PLAYING;
        s_is_state_transition = true;
    }
}

static void handleStateEnd()
{
    levelManagerEnd(s_is_state_transition, s_game_state_player_is_x);

    if (s_is_state_transition)
    {
        s_is_state_transition = false;
    }

    if (s_api_ptr->api.joystickIsAnyButtonPressed())
    {
        s_game_state = GAME_STATE_INIT;
        s_is_state_transition = true;
    }
}

void gameStateManagerUpdate(void)
{
    switch (s_game_state)
    {
    case GAME_STATE_INIT:
    {
        handleStateInit();
        break;
    }
    case GAME_STATE_CHOOSE_SYMBOL:
    {
        handleStateChooseSymbol();
        break;
    }
    case GAME_STATE_PLAYING:
    {
        handleStatePlaying();
        break;
    }
    case GAME_STATE_END:
    {
        handleStateEnd();
        break;
    }
    }
}