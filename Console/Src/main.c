#include <stm32f407xx.h>
#include "Peripherals/sysclock.h"
#include "game_console.h"
#include "MainMenu/main_menu.h"
#include "Peripherals/watchdog.h"
#include "Logger/logger.h"
#include "Flasher/flash_map.h"

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
        /* Feed the watchdog from the top of the idle/menu loop. gameLoaderLoadGame()
         * blocks for a game's lifetime but the kernel loop kicks per frame, and the
         * long flash/download paths kick from inside their own loops. */
        watchdogKick();
        /* The menu owns the screen until a game is selected; gameLoaderLoadGame()
         * blocks for the game's lifetime and the menu re-inits when it returns. */
        mainMenuUpdate();
        mainMenuRender();
    }
}
