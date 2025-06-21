#ifndef __GAME_LOADER_H
#define __GAME_LOADER_H
#include "game_console_api.h"

#define GAME_LOADER_RET_OK 0U
#define GAME_LOADER_RET_ERR 1U

uint8_t gameLoaderLoadGame(uint8_t binary_index);
uint8_t gameLoaderGetHeader(GameBinaryHeader *game_header);
uint8_t gameLoaderCloseGame();
#endif /* __GAME_LOADER_H */
