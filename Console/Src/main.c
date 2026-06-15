#include "console_config.h"
#include "sysclock.h"
#include "game_console.h"
#include "buzzer.h"
#include "renderer_testing.h"

void SystemInit(void)
{
    systemClockConfig();
}

int main(void)
{
    buzzerSetMute(true);
    gameConsoleInit();
    rendererTestingInit();

    while (1)
    {
        rendererTestingUpdate();
        rendererTestingRender();
    }
}
