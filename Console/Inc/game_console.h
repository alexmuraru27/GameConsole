#ifndef __GAME_CONSOLE_H
#define __GAME_CONSOLE_H

void gameConsoleInit(void);

/* Full software reset of the MCU: re-enters the reset vector and reruns the
 * whole boot sequence. Does not return. */
void gameConsoleReboot(void);
#endif /* __GAME_CONSOLE_H */
