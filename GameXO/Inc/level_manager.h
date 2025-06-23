
#ifndef __LEVEL_MANAGER_H
#define __LEVEL_MANAGER_H
#include <stdint.h>
#include "stdbool.h"
void levelManagerInit();

void levelManagerChooseSymbol(bool is_level_transition, bool is_player_x);
// Returns true if game is in progress
bool levelManagerPlay(bool is_level_transition, bool is_player_x);
void levelManagerEnd(bool is_level_transition, bool is_player_x);

#endif /* __LEVEL_MANAGER_H */