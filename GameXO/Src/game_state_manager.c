#include "game_state_manager.h"
#include "game_console_api.h"
#include "game_assets.h"
#include "tic_tac_toe_logic.h"
#include "GameXOAssetEnum.h"
#include <string.h>

DECLARE_API_HEADER_PTR(s_api);
#define API (s_api->api)

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Slate + cyan palette, matching the console menu. */
#define COL_BG RGB(16, 20, 28)
#define COL_GRID RGB(70, 120, 140)
#define COL_TITLE RGB(120, 220, 235)
#define COL_TEXT RGB(206, 220, 232)
#define COL_WIN RGB(90, 220, 120)
#define COL_LOSE RGB(235, 90, 90)
#define COL_DRAW RGB(240, 180, 60)

/* Board geometry (320x240). */
#define CELL 56
#define MARK 40
#define MARK_OFF ((CELL - MARK) / 2)
#define BOARD_W (3 * CELL)
#define BOARD_X ((320 - BOARD_W) / 2)
#define BOARD_Y 56
#define GRID_T 4 /* grid line thickness */

#define INPUT_DEBOUNCE_MS 200U

typedef enum
{
    PHASE_CHOOSE,
    PHASE_PLAYING,
    PHASE_END
} Phase;

static uint8_t s_board[3][3];
static bool s_player_is_x;
static uint8_t s_cursor_x, s_cursor_y;
static Phase s_phase;
static uint8_t s_result;
static bool s_result_handled;
static uint32_t s_last_input_ms;

/* Loaded once into the CCM arena; reused every frame. */
static Sprite s_spr_x, s_spr_o, s_spr_cursor;
static uint16_t s_pal_x[16], s_pal_o[16], s_pal_cursor[4];

/* Grid line tiles: solid (index 1) 2bpp, built once. 0x55 = four index-1 px. */
static uint8_t s_vline[GRID_T * BOARD_W / 4]; /* 4 wide x 168 tall */
static uint8_t s_hline[BOARD_W * GRID_T / 4]; /* 168 wide x 4 tall */
static const uint16_t s_pal_grid[4] = {0, COL_GRID, COL_GRID, COL_GRID};

/* Font palettes (2bpp: slot 0 transparent, ink in 1-3). */
static const uint16_t s_pal_title[4] = {0, COL_TITLE, COL_TITLE, COL_TITLE};
static const uint16_t s_pal_text[4] = {0, COL_TEXT, COL_TEXT, COL_TEXT};

/* Per-frame sprite scratch + scaled-text pool. */
#define UI_MAX 96U
static Sprite s_fg[32];
static Sprite s_ui[UI_MAX];
static uint8_t s_text_pool[2048];

static bool debounced(void)
{
    const uint32_t now = API.getSysTime();
    if (now > s_last_input_ms + INPUT_DEBOUNCE_MS)
    {
        s_last_input_ms = now;
        return true;
    }
    return false;
}

static Sprite placed(Sprite tmpl, int16_t x, int16_t y, uint8_t z)
{
    tmpl.x = x;
    tmpl.y = y;
    tmpl.z = z;
    return tmpl;
}

static void resetRound(void)
{
    ticTacToeInitBoard(s_board);
    s_player_is_x = true;
    s_cursor_x = 1U;
    s_cursor_y = 1U;
    s_result = TIC_TAC_TOE_GAME_STATE_CONTINUE;
    s_result_handled = false;
    s_phase = PHASE_CHOOSE;
}

void gameStateManagerInit(void)
{
    API.rendererInit();
    API.rendererSetBackground(COL_BG);

    gameAssetsInit();
    gameAssetsLoadSprite(GAMEXO_GFX_MARK_X, &s_spr_x, s_pal_x);
    gameAssetsLoadSprite(GAMEXO_GFX_MARK_O, &s_spr_o, s_pal_o);
    gameAssetsLoadSprite(GAMEXO_GFX_CURSOR, &s_spr_cursor, s_pal_cursor);

    for (uint16_t i = 0U; i < sizeof(s_vline); i++)
    {
        s_vline[i] = 0x55U;
    }
    for (uint16_t i = 0U; i < sizeof(s_hline); i++)
    {
        s_hline[i] = 0x55U;
    }

    gameAssetsPlaySound(GAMEXO_SFX_INTRO);
    resetRound();
    API.log("GameXO ready");
}

/* ---- per-mark helpers ------------------------------------------------ */

static const Sprite *markFor(uint8_t cell)
{
    if (cell == TIC_TAC_TOE_BOARD_PLAYER_X)
    {
        return &s_spr_x;
    }
    if (cell == TIC_TAC_TOE_BOARD_PLAYER_O)
    {
        return &s_spr_o;
    }
    return NULL;
}

static uint16_t buildGrid(uint16_t n)
{
    s_fg[n++] = (Sprite){.x = BOARD_X + CELL - GRID_T / 2, .y = BOARD_Y, .w = GRID_T, .h = BOARD_W,
                         .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_vline, .palette = s_pal_grid};
    s_fg[n++] = (Sprite){.x = BOARD_X + 2 * CELL - GRID_T / 2, .y = BOARD_Y, .w = GRID_T, .h = BOARD_W,
                         .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_vline, .palette = s_pal_grid};
    s_fg[n++] = (Sprite){.x = BOARD_X, .y = BOARD_Y + CELL - GRID_T / 2, .w = BOARD_W, .h = GRID_T,
                         .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_hline, .palette = s_pal_grid};
    s_fg[n++] = (Sprite){.x = BOARD_X, .y = BOARD_Y + 2 * CELL - GRID_T / 2, .w = BOARD_W, .h = GRID_T,
                         .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_hline, .palette = s_pal_grid};
    return n;
}

static uint16_t buildMarks(uint16_t n)
{
    for (uint8_t row = 0U; row < 3U; row++)
    {
        for (uint8_t col = 0U; col < 3U; col++)
        {
            const Sprite *mark = markFor(s_board[row][col]);
            if (mark != NULL)
            {
                const int16_t x = (int16_t)(BOARD_X + col * CELL + MARK_OFF);
                const int16_t y = (int16_t)(BOARD_Y + row * CELL + MARK_OFF);
                s_fg[n++] = placed(*mark, x, y, 2U);
            }
        }
    }
    return n;
}

/* ---- rendering per phase --------------------------------------------- */

static void renderChoose(void)
{
    const int16_t left_x = 100, right_x = 172, mark_y = 120;
    uint16_t nf = 0U, nu = 0U;

    const int16_t sel_x = s_player_is_x ? left_x : right_x;
    s_fg[nf++] = placed(s_spr_cursor, (int16_t)(sel_x - MARK_OFF), (int16_t)(mark_y - MARK_OFF), 1U);
    s_fg[nf++] = placed(s_spr_x, left_x, mark_y, 2U);
    s_fg[nf++] = placed(s_spr_o, right_x, mark_y, 2U);

    nu += gameAssetsDrawTextScaled(s_ui + nu, UI_MAX - nu, FONT_8x8,
                                   (int16_t)((320 - 11 * 18) / 2), 12, 0U, 2U,
                                   s_pal_title, "TIC TAC TOE", s_text_pool, sizeof(s_text_pool));
    const char *prompt = "PICK YOUR MARK";
    nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_8x8,
                             (int16_t)((320 - gameAssetsTextWidth(FONT_8x8, prompt)) / 2), 78, 0U,
                             s_pal_text, prompt);
    const char *hint = "LEFT / RIGHT  choose      A  start";
    nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_5x5,
                             (int16_t)((320 - gameAssetsTextWidth(FONT_5x5, hint)) / 2), 210, 0U,
                             s_pal_text, hint);

    API.rendererClear();
    API.rendererSubmitLayer(LAYER_FG, s_fg, nf);
    API.rendererSubmitLayer(LAYER_UI, s_ui, nu);
    API.rendererRender();
}

static void renderPlaying(void)
{
    uint16_t nf = 0U, nu = 0U;

    nf = buildGrid(nf);
    nf = buildMarks(nf);
    s_fg[nf++] = placed(s_spr_cursor,
                        (int16_t)(BOARD_X + s_cursor_x * CELL),
                        (int16_t)(BOARD_Y + s_cursor_y * CELL), 3U);

    nu += gameAssetsDrawTextScaled(s_ui + nu, UI_MAX - nu, FONT_8x8,
                                   (int16_t)((320 - 11 * 18) / 2), 12, 0U, 2U,
                                   s_pal_title, "TIC TAC TOE", s_text_pool, sizeof(s_text_pool));
    const char *hint = "D-PAD move      A  place      SP2  quit";
    nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_5x5,
                             (int16_t)((320 - gameAssetsTextWidth(FONT_5x5, hint)) / 2), 226, 0U,
                             s_pal_text, hint);

    API.rendererClear();
    API.rendererSubmitLayer(LAYER_FG, s_fg, nf);
    API.rendererSubmitLayer(LAYER_UI, s_ui, nu);
    API.rendererRender();
}

static void renderEnd(void)
{
    uint16_t nf = 0U, nu = 0U;

    nf = buildGrid(nf);
    nf = buildMarks(nf);

    const char *banner;
    const uint16_t *banner_pal;
    static const uint16_t pal_win[4] = {0, COL_WIN, COL_WIN, COL_WIN};
    static const uint16_t pal_lose[4] = {0, COL_LOSE, COL_LOSE, COL_LOSE};
    static const uint16_t pal_draw[4] = {0, COL_DRAW, COL_DRAW, COL_DRAW};

    if (s_result == TIC_TAC_TOE_GAME_STATE_DRAW)
    {
        banner = "DRAW";
        banner_pal = pal_draw;
    }
    else
    {
        const bool player_won = (s_result == TIC_TAC_TOE_GAME_STATE_WIN_X) == s_player_is_x;
        banner = player_won ? "YOU WIN" : "YOU LOSE";
        banner_pal = player_won ? pal_win : pal_lose;
    }

    /* A scaled banner across the middle of the board (z above the marks). */
    const int16_t banner_w = (int16_t)(strlen(banner) * (8 * 3 + 3));
    nu += gameAssetsDrawTextScaled(s_ui + nu, UI_MAX - nu, FONT_8x8,
                                   (int16_t)((320 - banner_w) / 2), 118, 0U, 3U,
                                   banner_pal, banner, s_text_pool, sizeof(s_text_pool));
    const char *hint = "A  play again        SP2  quit";
    nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_5x5,
                             (int16_t)((320 - gameAssetsTextWidth(FONT_5x5, hint)) / 2), 226, 0U,
                             s_pal_text, hint);

    API.rendererClear();
    API.rendererSubmitLayer(LAYER_FG, s_fg, nf);
    API.rendererSubmitLayer(LAYER_UI, s_ui, nu);
    API.rendererRender();
}

/* ---- input + logic per phase ----------------------------------------- */

static void updateChoose(void)
{
    if ((API.joystickGetRBtnLeft() || API.joystickGetRBtnRight()) && debounced())
    {
        s_player_is_x = !s_player_is_x;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
    if (API.joystickGetSpecialBtn1() && debounced())
    {
        s_phase = PHASE_PLAYING;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
}

static void aiMove(void)
{
    const uint8_t player = s_player_is_x ? TIC_TAC_TOE_BOARD_PLAYER_X : TIC_TAC_TOE_BOARD_PLAYER_O;
    const uint8_t ai = s_player_is_x ? TIC_TAC_TOE_BOARD_PLAYER_O : TIC_TAC_TOE_BOARD_PLAYER_X;
    uint8_t r = 0xFFU, c = 0xFFU;
    ticTacToeFindBestMove(s_board, ai, player, &r, &c);
    if (r < 3U && c < 3U)
    {
        ticTacToeMakeMove(s_board, r, c, ai);
    }
}

static void updatePlaying(void)
{
    if (API.joystickGetRBtnLeft() && s_cursor_x > 0U && debounced())
    {
        s_cursor_x--;
    }
    else if (API.joystickGetRBtnRight() && s_cursor_x < 2U && debounced())
    {
        s_cursor_x++;
    }
    else if (API.joystickGetRBtnUp() && s_cursor_y > 0U && debounced())
    {
        s_cursor_y--;
    }
    else if (API.joystickGetRBtnDown() && s_cursor_y < 2U && debounced())
    {
        s_cursor_y++;
    }
    else if (API.joystickGetSpecialBtn1() && debounced())
    {
        const uint8_t player = s_player_is_x ? TIC_TAC_TOE_BOARD_PLAYER_X : TIC_TAC_TOE_BOARD_PLAYER_O;
        if (ticTacToeMakeMove(s_board, s_cursor_y, s_cursor_x, player))
        {
            gameAssetsPlaySound(GAMEXO_SFX_MOVE);
            if (ticTacToeGetGameState(s_board) == TIC_TAC_TOE_GAME_STATE_CONTINUE)
            {
                aiMove();
            }
            s_result = ticTacToeGetGameState(s_board);
            if (s_result != TIC_TAC_TOE_GAME_STATE_CONTINUE)
            {
                s_phase = PHASE_END;
                s_result_handled = false;
            }
        }
    }
}

static void updateEnd(void)
{
    if (!s_result_handled)
    {
        s_result_handled = true;
        s_last_input_ms = API.getSysTime(); /* hold off the restart press briefly */
        if (s_result == TIC_TAC_TOE_GAME_STATE_DRAW)
        {
            gameAssetsPlaySound(GAMEXO_SFX_DRAW);
        }
        else
        {
            const bool player_won = (s_result == TIC_TAC_TOE_GAME_STATE_WIN_X) == s_player_is_x;
            gameAssetsPlaySound(player_won ? GAMEXO_SFX_WIN : GAMEXO_SFX_LOSE);
        }
    }
    if (API.joystickGetSpecialBtn1() && debounced())
    {
        resetRound();
    }
}

void gameStateManagerUpdate(void)
{
    switch (s_phase)
    {
    case PHASE_CHOOSE:
        updateChoose();
        renderChoose();
        break;
    case PHASE_PLAYING:
        updatePlaying();
        renderPlaying();
        break;
    case PHASE_END:
        updateEnd();
        renderEnd();
        break;
    }
}
