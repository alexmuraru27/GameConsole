#include <stm32f407xx.h>
#include "sysclock.h"
#include "usart.h"
#include "renderer.h"
#include "game_console.h"
#include "stddef.h"
#include "string.h"
#include "main_menu.h"

bool is_debug_fps = false;
#define FPS 50
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
  if (is_debug_fps)
  {
    debugInt(1000 / (getSysTime() - s_last_frame_time));
    debugString("\r\n");
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
  gameConsoleInit();
  mainMenuInit();
  while (1)
  {
    update();
    render();

    syncFrame();
  }
}
