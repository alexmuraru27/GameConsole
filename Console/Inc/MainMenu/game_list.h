#ifndef __GAME_LIST_H
#define __GAME_LIST_H

#include "menu_common.h"

/* The game picker screen: lists the SD-card .bin games, browses with up/down, and
 * launches the highlighted one with a tap of Special Button 1. Holding Special
 * Button 1 arms a delete (confirmed on a second screen) that removes the game's
 * .bin and its paired .pak. Special Button 2 returns to the root menu. Launching
 * blocks for the game's lifetime; on return the renderer surface is rebuilt and
 * the picker resumes. */
void gameListEnter(void);            /* (re)load the game list, reset the surface */
MenuTransition gameListUpdate(void); /* poll input, move/launch; STAY or GOTO_ROOT */
void gameListRender(void);           /* compose one picker frame */

#endif /* __GAME_LIST_H */
