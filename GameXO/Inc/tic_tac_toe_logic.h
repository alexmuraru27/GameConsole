
#ifndef __TIC_TAC_TOE_LOGIC_H
#define __TIC_TAC_TOE_LOGIC_H
#include <stdint.h>
#include "stdbool.h"

#define TIC_TAC_TOE_BOARD_EMPTY '0'
#define TIC_TAC_TOE_BOARD_PLAYER_X 'X'
#define TIC_TAC_TOE_BOARD_PLAYER_O 'O'

#define TIC_TAC_TOE_GAME_STATE_CONTINUE 0
#define TIC_TAC_TOE_GAME_STATE_WIN_X 1
#define TIC_TAC_TOE_GAME_STATE_WIN_O 2
#define TIC_TAC_TOE_GAME_STATE_DRAW 3

uint8_t ticTacToeGetGameState(uint8_t board[3][3]);
void ticTacToeInitBoard(uint8_t board[3][3]);
bool ticTacToeMakeMove(uint8_t board[3][3], uint8_t row, uint8_t col, uint8_t player);
void ticTacToeFindBestMove(uint8_t board[3][3], uint8_t ai_player, uint8_t human_player, uint8_t *best_row, uint8_t *best_col);

#endif /* __TIC_TAC_TOE_LOGIC_H */