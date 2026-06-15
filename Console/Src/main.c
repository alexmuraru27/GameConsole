#include <stm32f407xx.h>
#include "sysclock.h"
#include "renderer.h"
#include "game_console.h"
#include "stddef.h"
#include "string.h"
#include "main_menu.h"
#include "stdio.h"
#include "buzzer.h"
#include "renderer_testing.h"
#include "logger.h"

#define RENDERER_TESTING
// #define MAIN_SYNC_FPS

#define FPS 60
#define FRAME_PERIOD (1000U / FPS)
uint32_t s_last_frame_time = 0U;

void SystemInit(void)
{
    systemClockConfig();
}

static void syncFrame()
{
    if (getSysTime() - s_last_frame_time < FRAME_PERIOD)
    {
        // Busy-wait until it's time for the next frame
        while ((getSysTime() - s_last_frame_time) < FRAME_PERIOD)
            ;
    }
    s_last_frame_time = getSysTime(); // Keep consistent frame timing
}

static void init()
{
    // mainMenuInit();
#ifdef RENDERER_TESTING
    rendererTestingInit();
#endif
}

static void update()
{
    // mainMenuUpdate();
#ifdef RENDERER_TESTING
    rendererTestingUpdate();
#endif
}

static void render()
{
    // rendererRender();
#ifdef RENDERER_TESTING
    rendererTestingRender();
#endif
}

int main(void)
{
    buzzerSetMute(true);
    gameConsoleInit();

    init();
    LOGGER_LOG_INFO(LOGGER_CORE, "Boot OK");
    int i = 0;
    while (1)
    {
        if (i % (FPS * 10) == 0)
        {
            LOGGER_LOG_INFO(LOGGER_CORE, "Heartbeat %ds", i / FPS);
        }
        i++;
        update();
        render();

#ifdef MAIN_SYNC_FPS
        syncFrame();
#endif
    }
}
