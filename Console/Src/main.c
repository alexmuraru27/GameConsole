#include "sysclock.h"
#include <stm32f407xx.h>
#include "usart.h"
#include "gpio.h"
#include "dma.h"
#include "ILI9341.h"
#include "adc.h"
#include "timer.h"
#include "joystick.h"
#include "renderer.h"
#include "pacman_ghost.h"

#define FPS 50
#define FRAME_PERIOD (1000U / FPS)
uint32_t s_last_frame_time = 0U;

void SystemInit(void)
{
  systemClockConfig();
}

static void peripheralsInit()
{
  dmaInit();
  gpioInit();
  usartInit();
  timerInit();
  ili9341Init(3U, rendererGetSizeWidth(), rendererGetSizeHeight());
  adcInit();
  joystickInit();
}

static void consoleInit()
{
  peripheralsInit();
  rendererInit();
}

// TODO Remove statics :)
static uint8_t x = 0U;
static uint8_t y = 0U;
const uint16_t SPEED = 5U;

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
  if (x <= rendererGetSizeWidth() - rendererGetSizeTileScreen() - SPEED)
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x >= 0U + SPEED)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y <= rendererGetSizeHeight() - rendererGetSizeTileScreen() - SPEED)
  {
    y += ((joystickGetLAnalogY() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogY() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (y >= 0U + SPEED)
  {
    y -= ((joystickGetLAnalogY() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogY() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  rendererOamSetXPos(0U, x);
  rendererOamSetYPos(0U, y);
}

static void render()
{
  if (joystickGetSpecialBtn1())
  {
    ili9341FillWindow(ILI9341_BLACK);
  }
  if (joystickGetSpecialBtn2())
  {
    ili9341FillWindow(ILI9341_WHITE);
  }

  rendererRender();
}

static void screenInit()
{
  rendererPatternTableSetTile(1U, pacman_ghost_data, sizeof(pacman_ghost_data));
  rendererFramePaletteSetSpriteMultiple(0U, pacman_ghost_palette[1U], pacman_ghost_palette[2U], pacman_ghost_palette[3U]);
  rendererOamSetTileIdx(0U, 1U);
  rendererOamSetPaletteIdx(0U, 0U);
}
int main(void)
{
  consoleInit();
  screenInit();

  while (1)
  {
    update();
    render();

    syncFrame();
  }
}
