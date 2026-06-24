#include "game_state_manager.h"
#include "game_console_api.h"
#include "game_assets.h"
#include "tic_tac_toe_logic.h"
#include "xo_net.h"
#include "GameXOAssetEnum.h"
#include <string.h>
#include <stdio.h>


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

/* Host re-broadcasts the authoritative state at least this often so a dropped
 * snapshot self-heals even when the board is otherwise idle. */
#define MP_STATE_REBROADCAST_MS 500U

typedef enum
{
    PHASE_MODE_SELECT, /* Single / Host / Join                         */
    PHASE_HOST_LOBBY,  /* hosting, waiting for an opponent to join      */
    PHASE_JOIN_BROWSE, /* listing discovered hosts, pick one to join    */
    PHASE_CHOOSE,      /* single-player: pick X or O                    */
    PHASE_PLAYING,
    PHASE_END
} Phase;

static uint8_t s_board[3][3];
static bool s_player_is_x; /* SP: the chosen mark. MP: true iff we are the host (X). */
static uint8_t s_cursor_x, s_cursor_y;
static Phase s_phase;
static uint8_t s_result;
static bool s_result_handled;
static uint32_t s_last_input_ms;

/* ---- multiplayer state ---- */
static bool s_is_mp;          /* the current game is networked            */
static bool s_self_is_host;   /* our role this game                       */
static uint8_t s_self_index;  /* our player index (host = 0)              */
static uint8_t s_opp_index;   /* the opponent's player index             */
static uint8_t s_turn;        /* player index whose turn it is            */
static bool s_opp_left;       /* opponent dropped mid-game                */
static bool s_have_state;     /* client: a snapshot has arrived           */
static uint32_t s_last_state_ms;
static uint8_t s_mode_sel;    /* highlighted row in PHASE_MODE_SELECT     */
static uint8_t s_join_sel;    /* highlighted row in PHASE_JOIN_BROWSE     */
static bool s_joining;        /* a join request is in flight              */
static MpHostInfo s_hosts[8];
static int s_host_count;
static char s_self_name[MP_NAME_MAX + 1];
static char s_opp_name[MP_NAME_MAX + 1];

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
static const uint16_t s_pal_win[4] = {0, COL_WIN, COL_WIN, COL_WIN}; /* "your turn" / "joined" */

/* Per-frame sprite scratch + scaled-text pool. */
#define UI_MAX 96U
static Sprite s_fg[32];
static Sprite s_ui[UI_MAX];
static uint8_t s_text_pool[2048];

static bool debounced(void)
{
    const uint32_t now = getSysTime();
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

static void enterModeSelect(void)
{
    s_is_mp = false;
    s_opp_left = false;
    s_joining = false;
    s_mode_sel = 0U;
    s_phase = PHASE_MODE_SELECT;
    s_last_input_ms = getSysTime();
}

/* Single-player round: empty board, you are X, AI is O. */
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
    rendererInit();
    rendererSetBackground(COL_BG);

    s_last_input_ms = getSysTime();

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
    enterModeSelect();
    gameLog("GameXO ready");
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
    s_fg[n++] = (Sprite){.x = BOARD_X + CELL - GRID_T / 2, .y = BOARD_Y, .w = GRID_T, .h = BOARD_W, .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_vline, .palette = s_pal_grid};
    s_fg[n++] = (Sprite){.x = BOARD_X + 2 * CELL - GRID_T / 2, .y = BOARD_Y, .w = GRID_T, .h = BOARD_W, .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_vline, .palette = s_pal_grid};
    s_fg[n++] = (Sprite){.x = BOARD_X, .y = BOARD_Y + CELL - GRID_T / 2, .w = BOARD_W, .h = GRID_T, .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_hline, .palette = s_pal_grid};
    s_fg[n++] = (Sprite){.x = BOARD_X, .y = BOARD_Y + 2 * CELL - GRID_T / 2, .w = BOARD_W, .h = GRID_T, .z = 0U, .flags = SPRITE_OPAQUE, .pixels = s_hline, .palette = s_pal_grid};
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

/* Centered single-line text helper for the lobby screens. */
static uint16_t drawCentered(uint16_t n, FontSize font, int16_t y, const uint16_t *pal, const char *s)
{
    const int16_t x = (int16_t)((320 - gameAssetsTextWidth(font, s)) / 2);
    return n + gameAssetsDrawText(s_ui + n, UI_MAX - n, font, x, y, 0U, pal, s);
}

/* ---- multiplayer helpers --------------------------------------------- */

static void flattenBoard(uint8_t out[9])
{
    for (uint8_t r = 0U; r < 3U; r++)
    {
        for (uint8_t c = 0U; c < 3U; c++)
        {
            out[r * 3U + c] = s_board[r][c];
        }
    }
}

static void adoptBoard(const uint8_t in[9])
{
    for (uint8_t r = 0U; r < 3U; r++)
    {
        for (uint8_t c = 0U; c < 3U; c++)
        {
            s_board[r][c] = in[r * 3U + c];
        }
    }
}

static void hostBroadcastState(void)
{
    uint8_t flat[9];
    flattenBoard(flat);
    xoNetBroadcastState(flat, s_turn, s_result);
    s_last_state_ms = getSysTime();
}

/* The other connected player (2-player game). Falls back to self if none. */
static uint8_t findOpponent(void)
{
    const uint8_t self = mpGetSelfIndex();
    for (uint8_t i = 0U; i < MP_MAX_PLAYERS; i++)
    {
        if (i != self && mpIsConnected(i))
        {
            return i;
        }
    }
    return self;
}

static void startNetworkGame(bool as_host)
{
    s_is_mp = true;
    s_self_is_host = as_host;
    s_self_index = mpGetSelfIndex();
    s_opp_index = findOpponent();
    s_player_is_x = as_host; /* host plays X; lets the SP win/lose logic work as-is */
    s_opp_left = false;
    s_result = TIC_TAC_TOE_GAME_STATE_CONTINUE;
    s_result_handled = false;
    s_cursor_x = 1U;
    s_cursor_y = 1U;
    mpGetSelfName(s_self_name, sizeof(s_self_name));
    mpGetName(s_opp_index, s_opp_name, sizeof(s_opp_name));

    if (as_host)
    {
        ticTacToeInitBoard(s_board);
        s_turn = 0U;          /* host (X) moves first */
        s_have_state = true;  /* host always holds the authoritative board */
        hostBroadcastState();
    }
    else
    {
        ticTacToeInitBoard(s_board);
        s_turn = 0U;
        s_have_state = false; /* wait for the host's first snapshot */
    }
    s_phase = PHASE_PLAYING;
    gameLog("MP game start: %s vs %s (host=%d)", s_self_name, s_opp_name, (int)as_host);
}

static void leaveNetworkGame(void)
{
    mpStop();
    enterModeSelect();
}

/* ---- mode select ----------------------------------------------------- */

static const char *const s_mode_labels[] = {"SINGLE PLAYER", "HOST GAME", "JOIN GAME"};
#define MODE_COUNT (sizeof(s_mode_labels) / sizeof(s_mode_labels[0]))

static void updateModeSelect(void)
{
    if (joystickGetRBtnDown() && s_mode_sel + 1U < MODE_COUNT && debounced())
    {
        s_mode_sel++;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
    else if (joystickGetRBtnUp() && s_mode_sel > 0U && debounced())
    {
        s_mode_sel--;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
    else if (joystickGetSpecialBtn1() && debounced())
    {
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
        if (s_mode_sel == 0U)
        {
            resetRound(); /* single-player */
        }
        else if (s_mode_sel == 1U)
        {
            if (mpHostStart() == MP_OK)
            {
                s_phase = PHASE_HOST_LOBBY;
            }
        }
        else
        {
            if (mpJoinStart() == MP_OK)
            {
                s_join_sel = 0U;
                s_joining = false;
                s_host_count = 0;
                s_phase = PHASE_JOIN_BROWSE;
            }
        }
    }
}

static void renderModeSelect(void)
{
    uint16_t nu = 0U;
    nu += gameAssetsDrawTextScaled(s_ui + nu, UI_MAX - nu, FONT_8x8,
                                   (int16_t)((320 - 11 * 18) / 2), 28, 0U, 2U,
                                   s_pal_title, "TIC TAC TOE", s_text_pool, sizeof(s_text_pool));

    for (uint8_t i = 0U; i < MODE_COUNT; i++)
    {
        const int16_t y = (int16_t)(100 + i * 30);
        const bool sel = (i == s_mode_sel);
        if (sel)
        {
            const int16_t cx = (int16_t)((320 - gameAssetsTextWidth(FONT_8x8, s_mode_labels[i])) / 2 - 20);
            nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_8x8, cx, y, 0U, s_pal_title, ">");
        }
        nu = drawCentered(nu, FONT_8x8, y, sel ? s_pal_title : s_pal_text, s_mode_labels[i]);
    }

    nu = drawCentered(nu, FONT_5x5, 222, s_pal_text, "UP/DOWN choose    A select    SP2 quit");

    rendererClear();
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
    rendererRender();
}

/* ---- host lobby ------------------------------------------------------ */

static void updateHostLobby(void)
{
    const bool opponent_here = (mpGetPlayerCount() >= 2U);
    if (opponent_here && joystickGetSpecialBtn1() && debounced())
    {
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
        startNetworkGame(true);
    }
}

static void renderHostLobby(void)
{
    uint16_t nu = 0U;
    nu += gameAssetsDrawTextScaled(s_ui + nu, UI_MAX - nu, FONT_8x8,
                                   (int16_t)((320 - 7 * 18) / 2), 24, 0U, 2U,
                                   s_pal_title, "HOSTING", s_text_pool, sizeof(s_text_pool));

    char line[40];
    char self_name[MP_NAME_MAX + 1];
    mpGetSelfName(self_name, sizeof(self_name));
    (void)snprintf(line, sizeof(line), "You: %s", self_name);
    nu = drawCentered(nu, FONT_8x8, 96, s_pal_text, line);

    const bool ready = (mpGetPlayerCount() >= 2U);
    if (ready)
    {
        char opp[MP_NAME_MAX + 1];
        mpGetName(findOpponent(), opp, sizeof(opp));
        (void)snprintf(line, sizeof(line), "%s joined!", opp);
        nu = drawCentered(nu, FONT_8x8, 130, s_pal_win, line);
        nu = drawCentered(nu, FONT_5x5, 222, s_pal_text, "A start    SP2 cancel");
    }
    else
    {
        const bool blink = ((getSysTime() / 400U) & 1U) == 0U;
        if (blink)
        {
            nu = drawCentered(nu, FONT_8x8, 130, s_pal_text, "Waiting for opponent...");
        }
        nu = drawCentered(nu, FONT_5x5, 222, s_pal_text, "SP2 cancel");
    }

    rendererClear();
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
    rendererRender();
}

/* ---- join browse ----------------------------------------------------- */

static void updateJoinBrowse(void)
{
    s_host_count = mpScanHosts(s_hosts, (int)(sizeof(s_hosts) / sizeof(s_hosts[0])));
    if (s_host_count < 0)
    {
        s_host_count = 0;
    }

    /* A join request is in flight: as soon as the host accepts (role flips to
     * CLIENT) we are in the game. */
    if (s_joining)
    {
        if (mpGetRole() == MP_ROLE_CLIENT)
        {
            gameAssetsPlaySound(GAMEXO_SFX_MOVE);
            startNetworkGame(false);
        }
        return;
    }

    if (s_host_count == 0)
    {
        return;
    }
    if (s_join_sel >= (uint8_t)s_host_count)
    {
        s_join_sel = (uint8_t)(s_host_count - 1);
    }

    if (joystickGetRBtnDown() && s_join_sel + 1U < (uint8_t)s_host_count && debounced())
    {
        s_join_sel++;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
    else if (joystickGetRBtnUp() && s_join_sel > 0U && debounced())
    {
        s_join_sel--;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
    else if (joystickGetSpecialBtn1() && debounced())
    {
        const MpStatus st = mpJoin(s_hosts[s_join_sel].handle);
        if (st == MP_PENDING || st == MP_OK)
        {
            s_joining = true;
            gameAssetsPlaySound(GAMEXO_SFX_MOVE);
        }
    }
}

static void renderJoinBrowse(void)
{
    uint16_t nu = 0U;
    nu += gameAssetsDrawTextScaled(s_ui + nu, UI_MAX - nu, FONT_8x8,
                                   (int16_t)((320 - 9 * 18) / 2), 24, 0U, 2U,
                                   s_pal_title, "JOIN GAME", s_text_pool, sizeof(s_text_pool));

    if (s_joining)
    {
        nu = drawCentered(nu, FONT_8x8, 110, s_pal_text, "Connecting...");
    }
    else if (s_host_count == 0)
    {
        const bool blink = ((getSysTime() / 400U) & 1U) == 0U;
        if (blink)
        {
            nu = drawCentered(nu, FONT_8x8, 110, s_pal_text, "Searching for hosts...");
        }
    }
    else
    {
        for (int i = 0; i < s_host_count && i < 5; i++)
        {
            const int16_t y = (int16_t)(80 + i * 26);
            const bool sel = ((uint8_t)i == s_join_sel);
            char line[40];
            (void)snprintf(line, sizeof(line), "%s (%u)", s_hosts[i].name, (unsigned)s_hosts[i].player_count);
            if (sel)
            {
                const int16_t cx = (int16_t)((320 - gameAssetsTextWidth(FONT_8x8, line)) / 2 - 20);
                nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_8x8, cx, y, 0U, s_pal_title, ">");
            }
            nu = drawCentered(nu, FONT_8x8, y, sel ? s_pal_title : s_pal_text, line);
        }
    }

    nu = drawCentered(nu, FONT_5x5, 222, s_pal_text,
                      s_joining ? "SP2 cancel" : "UP/DOWN pick    A join    SP2 back");

    rendererClear();
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
    rendererRender();
}

/* ---- single-player choose ------------------------------------------- */

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

    rendererClear();
    rendererSubmitLayer(LAYER_FG, s_fg, nf);
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
    rendererRender();
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

    if (s_is_mp)
    {
        const bool my_turn = (s_turn == s_self_index);
        const char *turn = my_turn ? "YOUR TURN" : "OPPONENT'S TURN";
        nu = drawCentered(nu, FONT_8x8, 40, my_turn ? s_pal_win : s_pal_text, turn);
        char names[56]; /* two names (<=16) + " X   vs   " + marks */
        (void)snprintf(names, sizeof(names), "%s %c   vs   %s %c",
                       s_self_name, s_self_is_host ? 'X' : 'O',
                       s_opp_name, s_self_is_host ? 'O' : 'X');
        nu = drawCentered(nu, FONT_5x5, 226, s_pal_text, names);
    }
    else
    {
        const char *hint = "D-PAD move      A  place      SP2  quit";
        nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_5x5,
                                 (int16_t)((320 - gameAssetsTextWidth(FONT_5x5, hint)) / 2), 226, 0U,
                                 s_pal_text, hint);
    }

    rendererClear();
    rendererSubmitLayer(LAYER_FG, s_fg, nf);
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
    rendererRender();
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

    if (s_opp_left)
    {
        banner = "OPPONENT LEFT";
        banner_pal = pal_draw;
    }
    else if (s_result == TIC_TAC_TOE_GAME_STATE_DRAW)
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
    const char *hint = s_is_mp ? "A  menu        SP2  quit" : "A  play again        SP2  quit";
    nu += gameAssetsDrawText(s_ui + nu, UI_MAX - nu, FONT_5x5,
                             (int16_t)((320 - gameAssetsTextWidth(FONT_5x5, hint)) / 2), 226, 0U,
                             s_pal_text, hint);

    rendererClear();
    rendererSubmitLayer(LAYER_FG, s_fg, nf);
    rendererSubmitLayer(LAYER_UI, s_ui, nu);
    rendererRender();
}

/* ---- input + logic per phase ----------------------------------------- */

static void updateChoose(void)
{
    if ((joystickGetRBtnLeft() || joystickGetRBtnRight()) && debounced())
    {
        s_player_is_x = !s_player_is_x;
        gameAssetsPlaySound(GAMEXO_SFX_MOVE);
    }
    if (joystickGetSpecialBtn1() && debounced())
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

/* Move the cursor with the d-pad (shared by single- and multi-player). */
static void moveCursor(void)
{
    if (joystickGetRBtnLeft() && s_cursor_x > 0U && debounced())
    {
        s_cursor_x--;
    }
    else if (joystickGetRBtnRight() && s_cursor_x < 2U && debounced())
    {
        s_cursor_x++;
    }
    else if (joystickGetRBtnUp() && s_cursor_y > 0U && debounced())
    {
        s_cursor_y--;
    }
    else if (joystickGetRBtnDown() && s_cursor_y < 2U && debounced())
    {
        s_cursor_y++;
    }
}

static void goToEnd(void)
{
    s_phase = PHASE_END;
    s_result_handled = false;
}

static void updatePlayingSingle(void)
{
    moveCursor();
    if (joystickGetSpecialBtn1() && debounced())
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
                goToEnd();
            }
        }
    }
}

static void updatePlayingHost(void)
{
    /* Apply the client's move intents (only on its turn, only to a legal cell). */
    XoMsg m;
    uint8_t src;
    while (xoNetPoll(&m, &src))
    {
        if (m.type == XO_MSG_MOVE && s_turn == s_opp_index &&
            ticTacToeMakeMove(s_board, m.r, m.c, TIC_TAC_TOE_BOARD_PLAYER_O))
        {
            gameAssetsPlaySound(GAMEXO_SFX_MOVE);
            s_result = ticTacToeGetGameState(s_board);
            s_turn = s_self_index;
            hostBroadcastState();
            if (s_result != TIC_TAC_TOE_GAME_STATE_CONTINUE)
            {
                goToEnd();
                return;
            }
        }
    }

    if (!mpIsConnected(s_opp_index))
    {
        s_opp_left = true;
        goToEnd();
        return;
    }

    /* Our own move on our turn. */
    if (s_turn == s_self_index)
    {
        moveCursor();
        if (joystickGetSpecialBtn1() && debounced() &&
            ticTacToeMakeMove(s_board, s_cursor_y, s_cursor_x, TIC_TAC_TOE_BOARD_PLAYER_X))
        {
            gameAssetsPlaySound(GAMEXO_SFX_MOVE);
            s_result = ticTacToeGetGameState(s_board);
            s_turn = s_opp_index;
            hostBroadcastState();
            if (s_result != TIC_TAC_TOE_GAME_STATE_CONTINUE)
            {
                goToEnd();
                return;
            }
        }
    }

    /* Periodic re-broadcast so a lost snapshot self-heals. */
    if ((getSysTime() - s_last_state_ms) > MP_STATE_REBROADCAST_MS)
    {
        hostBroadcastState();
    }
}

static void updatePlayingClient(void)
{
    /* Adopt the host's authoritative snapshots. */
    XoMsg m;
    uint8_t src;
    while (xoNetPoll(&m, &src))
    {
        if (m.type == XO_MSG_STATE)
        {
            adoptBoard(m.board);
            s_turn = m.turn;
            s_result = m.result;
            s_have_state = true;
            if (s_result != TIC_TAC_TOE_GAME_STATE_CONTINUE)
            {
                goToEnd();
                return;
            }
        }
    }

    if (!mpIsConnected(s_opp_index))
    {
        s_opp_left = true;
        goToEnd();
        return;
    }

    /* On our turn, send a move intent for an empty cell — the host is authoritative,
     * so we do not place locally; we wait for the snapshot. */
    if (s_have_state && s_turn == s_self_index)
    {
        moveCursor();
        if (joystickGetSpecialBtn1() && debounced() &&
            s_board[s_cursor_y][s_cursor_x] == TIC_TAC_TOE_BOARD_EMPTY)
        {
            xoNetSendMove(s_cursor_y, s_cursor_x);
            gameAssetsPlaySound(GAMEXO_SFX_MOVE);
        }
    }
}

static void updatePlaying(void)
{
    if (!s_is_mp)
    {
        updatePlayingSingle();
    }
    else if (s_self_is_host)
    {
        updatePlayingHost();
    }
    else
    {
        updatePlayingClient();
    }
}

static void updateEnd(void)
{
    if (!s_result_handled)
    {
        s_result_handled = true;
        s_last_input_ms = getSysTime(); /* hold off the restart press briefly */
        if (s_opp_left)
        {
            gameAssetsPlaySound(GAMEXO_SFX_LOSE);
        }
        else if (s_result == TIC_TAC_TOE_GAME_STATE_DRAW)
        {
            gameAssetsPlaySound(GAMEXO_SFX_DRAW);
        }
        else
        {
            const bool player_won = (s_result == TIC_TAC_TOE_GAME_STATE_WIN_X) == s_player_is_x;
            gameAssetsPlaySound(player_won ? GAMEXO_SFX_WIN : GAMEXO_SFX_LOSE);
        }
    }
    if (joystickGetSpecialBtn1() && debounced())
    {
        if (s_is_mp)
        {
            leaveNetworkGame(); /* one match per connection in v1 */
        }
        else
        {
            resetRound();
        }
    }
}

/* Special Button 2 backs out one level; at the top menu it asks main.c to quit.
 * Returns true only when the game should exit. */
static bool handleBack(void)
{
    if (!joystickGetSpecialBtn2() || !debounced())
    {
        return false;
    }
    switch (s_phase)
    {
    case PHASE_MODE_SELECT:
        return true; /* quit GameXO */
    case PHASE_HOST_LOBBY:
    case PHASE_JOIN_BROWSE:
        leaveNetworkGame();
        break;
    case PHASE_CHOOSE:
        enterModeSelect();
        break;
    case PHASE_PLAYING:
    case PHASE_END:
        if (s_is_mp)
        {
            leaveNetworkGame();
        }
        else
        {
            enterModeSelect();
        }
        break;
    }
    return false;
}

/* The OS drives update and render as separate steps each frame. Logic runs first
 * (it may advance the phase); the matching draw then reflects the new phase. */
bool gameStateManagerUpdate(void)
{
    if (handleBack())
    {
        return true;
    }

    switch (s_phase)
    {
    case PHASE_MODE_SELECT:
        updateModeSelect();
        break;
    case PHASE_HOST_LOBBY:
        updateHostLobby();
        break;
    case PHASE_JOIN_BROWSE:
        updateJoinBrowse();
        break;
    case PHASE_CHOOSE:
        updateChoose();
        break;
    case PHASE_PLAYING:
        updatePlaying();
        break;
    case PHASE_END:
        updateEnd();
        break;
    }
    return false;
}

void gameStateManagerRender(void)
{
    switch (s_phase)
    {
    case PHASE_MODE_SELECT:
        renderModeSelect();
        break;
    case PHASE_HOST_LOBBY:
        renderHostLobby();
        break;
    case PHASE_JOIN_BROWSE:
        renderJoinBrowse();
        break;
    case PHASE_CHOOSE:
        renderChoose();
        break;
    case PHASE_PLAYING:
        renderPlaying();
        break;
    case PHASE_END:
        renderEnd();
        break;
    }
}
