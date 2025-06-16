#ifndef __GAME_LOADER_H
#define __GAME_LOADER_H
#include "game_console_api.h"

uint8_t gameLoaderLoadGame(uint8_t binary_index);
uint8_t gameLoaderCloseGame();
#endif /* __GAME_LOADER_H */
