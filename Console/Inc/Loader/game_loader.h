#ifndef __GAME_LOADER_H
#define __GAME_LOADER_H
#include "header_interface.h"

#define GAME_LOADER_RET_OK 0U
#define GAME_LOADER_RET_ERR 1U
#define GAME_LOADER_RET_CRASHED 2U /* game faulted; console recovered and returned */

uint8_t gameLoaderLoadGame(uint8_t binary_index);
uint8_t gameLoaderGetHeader(GameBinaryHeader *game_header);
uint8_t gameLoaderCloseGame();

/* Microseconds between the last two update() calls (0 on a game's first frame),
 * clamped to a sane maximum. Backs the getDeltaTimeUs() syscall. */
uint32_t gameLoaderGetDeltaUs(void);
#endif /* __GAME_LOADER_H */
