#include <stm32f407xx.h>
#include "sysclock.h"
#include "game_console.h"
#include "main_menu.h"
#include "logger.h"
#include "flash_map.h"

void SystemInit(void)
{
    /* The app is linked above the bootloader (sector 0), so point the vector table
     * at the app's table before any interrupt can fire. The bootloader hands off
     * here after validating us; on a plain reset this relocates VTOR from its
     * 0x08000000 reset default. */
    SCB->VTOR = APP_FLASH_ADDR;
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
