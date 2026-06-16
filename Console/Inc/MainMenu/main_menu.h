#ifndef __MAIN_MENU_H
#define __MAIN_MENU_H

/* The console's game-picker: lists the .bin games on the SD card, lets the
 * player browse with up/down, and launches the highlighted one with A. */
void mainMenuInit(void);
void mainMenuUpdate(void); /* poll input, move the selection */
void mainMenuRender(void); /* compose and present one menu frame */

#endif /* __MAIN_MENU_H */
