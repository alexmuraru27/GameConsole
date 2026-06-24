#include "tic_tac_toe_logic.h"

static bool isValidMove(const uint8_t board[3][3], const uint8_t row, const uint8_t col)
{
    return (row >= 0U && row < 3U && col >= 0U && col < 3U && board[row][col] == TIC_TAC_TOE_BOARD_EMPTY);
}

static bool checkWin(const uint8_t board[3][3], const uint8_t game_symbol)
{
    // rows
    for (int i = 0U; i < 3U; i++)
    {
        if (board[i][0U] == game_symbol && board[i][1U] == game_symbol && board[i][2U] == game_symbol)
        {
            return true;
        }
    }

    // columns
    for (int j = 0U; j < 3U; j++)
    {
        if (board[0U][j] == game_symbol && board[1U][j] == game_symbol && board[2U][j] == game_symbol)
        {
            return true;
        }
    }

    // diagonals
    if (board[0U][0U] == game_symbol && board[1U][1U] == game_symbol && board[2U][2U] == game_symbol)
    {
        return true;
    }
    if (board[0U][2U] == game_symbol && board[1U][1U] == game_symbol && board[2U][0U] == game_symbol)
    {
        return true;
    }

    return false;
}

static bool checkDraw(const uint8_t board[3][3])
{
    for (int i = 0U; i < 3U; i++)
    {
        for (int j = 0U; j < 3U; j++)
        {
            if (board[i][j] == TIC_TAC_TOE_BOARD_EMPTY)
            {
                return false;
            }
        }
    }
    return true;
}

static int alphabeta(uint8_t board[3][3], int depth, int alpha, int beta, bool isMaximizing, uint8_t ai_player, uint8_t human_player)
{
    // Terminal states
    if (checkWin(board, ai_player))
    {
        return 10 - depth;
    }
    if (checkWin(board, human_player))
    {
        return depth - 10;
    }
    if (checkDraw(board))
    {
        return 0;
    }

    if (isMaximizing)
    {
        int maxEval = -1000;
        for (uint8_t i = 0U; i < 3U; i++)
        {
            for (uint8_t j = 0U; j < 3U; j++)
            {
                if (board[i][j] == TIC_TAC_TOE_BOARD_EMPTY)
                {
                    board[i][j] = ai_player;
                    int eval = alphabeta(board, depth + 1, alpha, beta, false, ai_player, human_player);
                    board[i][j] = TIC_TAC_TOE_BOARD_EMPTY;

                    if (eval > maxEval)
                    {
                        maxEval = eval;
                    }
                    if (eval > alpha)
                    {
                        alpha = eval;
                    }
                    if (beta <= alpha)
                    {
                        return maxEval;
                    }
                }
            }
        }
        return maxEval;
    }
    else
    {
        int minEval = 1000;
        for (uint8_t i = 0U; i < 3U; i++)
        {
            for (uint8_t j = 0U; j < 3U; j++)
            {
                if (board[i][j] == TIC_TAC_TOE_BOARD_EMPTY)
                {
                    board[i][j] = human_player;
                    int eval = alphabeta(board, depth + 1, alpha, beta, true, ai_player, human_player);
                    board[i][j] = TIC_TAC_TOE_BOARD_EMPTY;

                    if (eval < minEval)
                    {
                        minEval = eval;
                    }
                    if (eval < beta)
                    {
                        beta = eval;
                    }

                    if (beta <= alpha)
                    {
                        return minEval;
                    }
                }
            }
        }
        return minEval;
    }
}

void ticTacToeInitBoard(uint8_t board[3][3])
{
    for (uint8_t i = 0U; i < 3U; i++)
    {
        for (uint8_t j = 0U; j < 3U; j++)
        {
            board[i][j] = TIC_TAC_TOE_BOARD_EMPTY;
        }
    }
}

uint8_t ticTacToeGetGameState(uint8_t board[3][3])
{
    if (checkWin(board, TIC_TAC_TOE_BOARD_PLAYER_X))
    {
        return TIC_TAC_TOE_GAME_STATE_WIN_X;
    }
    if (checkWin(board, TIC_TAC_TOE_BOARD_PLAYER_O))
    {
        return TIC_TAC_TOE_GAME_STATE_WIN_O;
    }
    if (checkDraw(board))
    {
        return TIC_TAC_TOE_GAME_STATE_DRAW;
    }
    return TIC_TAC_TOE_GAME_STATE_CONTINUE;
}

bool ticTacToeMakeMove(uint8_t board[3][3], uint8_t row, uint8_t col, uint8_t player)
{
    if (isValidMove(board, row, col))
    {
        board[row][col] = player;
        return true;
    }
    return false;
}

void ticTacToeFindBestMove(uint8_t board[3][3], uint8_t ai_player, uint8_t human_player, uint8_t *best_row, uint8_t *best_col)
{
    int bestValue = -1000;
    *best_row = 0xFF;
    *best_col = 0xFF;

    // Initialize alpha and beta for root node
    int alpha = -1000;
    int beta = 1000;

    for (uint8_t i = 0U; i < 3U; i++)
    {
        for (uint8_t j = 0U; j < 3U; j++)
        {
            if (board[i][j] == TIC_TAC_TOE_BOARD_EMPTY)
            {
                board[i][j] = ai_player;
                int moveValue = alphabeta(board, 0, alpha, beta, false, ai_player, human_player);
                board[i][j] = TIC_TAC_TOE_BOARD_EMPTY;

                if (moveValue > bestValue)
                {
                    bestValue = moveValue;
                    *best_row = i;
                    *best_col = j;
                }

                if (moveValue > alpha)
                {
                    alpha = moveValue;
                }
            }
        }
    }
}
