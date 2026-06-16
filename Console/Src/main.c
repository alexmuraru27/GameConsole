#include <stm32f407xx.h>
#include "sysclock.h"
#include "game_console.h"
#include "main_menu.h"
#include "logger.h"

void SystemInit(void)
{
    systemClockConfig();
}

int main(void)
{
    gameConsoleInit();
    mainMenuInit();
    LOGGER_LOG_INFO(LOGGER_CORE, "Boot OK");

    while (1)
    {
        /* The menu owns the screen until a game is selected; gameLoaderLoadGame()
         * blocks for the game's lifetime and the menu re-inits when it returns. */
        mainMenuUpdate();
        mainMenuRender();
    }
}
