#include <stm32f407xx.h>
#include "sysclock.h"
#include "renderer.h"
#include "game_console.h"
#include "stddef.h"
#include "string.h"
#include "main_menu.h"
#include "stdio.h"
#include "buzzer.h"

#define FPS 30
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

static void update()
{
  mainMenuUpdate();
}

static void render()
{
  rendererRender();
}

int main(void)
{
  // TODO Temporary mute
  buzzerSetMute(true);
  gameConsoleInit();
  mainMenuInit();

  printf("Boot OK\n");
  int i = 0;
  while (1)
  {
    if (i % (FPS * 10) == 0)
    {
      printf("Heartbeat %ds\n", i / FPS);
    }
    i++;
    update();
    render();

    syncFrame();
  }
}
