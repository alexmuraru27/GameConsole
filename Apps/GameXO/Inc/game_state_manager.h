
#ifndef __GAME_STATE_MANAGER_H
#define __GAME_STATE_MANAGER_H
#include <stdint.h>
#include <stdbool.h>

void gameStateManagerInit(void);
/* Drives one frame of input/logic. Returns true when the player asks to quit the
 * game (Special Button 2 at the top-level mode menu), so main.c can gameExit(). */
bool gameStateManagerUpdate(void);
void gameStateManagerRender(void);
#endif /* __GAME_STATE_MANAGER_H */