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
#include "pacman1.h"
#include "pacman2.h"
#include "pacman3.h"
#include "pacman4.h"
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
  ili9341Init(3U, RENDERER_WIDTH, RENDERER_HEIGHT);
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
  if (x <= RENDERER_WIDTH - RENDERER_TILE_SCREEN_SIZE - SPEED)
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x >= 0U + SPEED)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y <= RENDERER_HEIGHT - RENDERER_TILE_SCREEN_SIZE - SPEED)
  {
    y += ((joystickGetLAnalogY() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogY() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (y >= 0U + SPEED)
  {
    y -= ((joystickGetLAnalogY() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogY() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  rendererOamSetXPos(0U, x);
  rendererOamSetYPos(0U, y);

  rendererOamSetXPos(1U, x + RENDERER_TILE_SCREEN_SIZE);
  rendererOamSetYPos(1U, y);

  rendererOamSetXPos(2U, x);
  rendererOamSetYPos(2U, y + RENDERER_TILE_SCREEN_SIZE);

  rendererOamSetXPos(3U, x + RENDERER_TILE_SCREEN_SIZE);
  rendererOamSetYPos(3U, y + RENDERER_TILE_SCREEN_SIZE);
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
  rendererPatternTableSetTile(1U, pacman1_data, sizeof(pacman1_data));
  rendererPatternTableSetTile(2U, pacman2_data, sizeof(pacman2_data));
  rendererPatternTableSetTile(3U, pacman3_data, sizeof(pacman3_data));
  rendererPatternTableSetTile(4U, pacman4_data, sizeof(pacman4_data));
  rendererPaletteSetSpriteMultiple(0U, pacman1_palette[1U], pacman1_palette[2U], pacman1_palette[3U]);
  rendererPaletteSetSpriteMultiple(1U, pacman2_palette[1U], pacman2_palette[2U], pacman2_palette[3U]);
  rendererPaletteSetSpriteMultiple(2U, pacman3_palette[1U], pacman3_palette[2U], pacman3_palette[3U]);
  rendererPaletteSetSpriteMultiple(3U, pacman4_palette[1U], pacman4_palette[2U], pacman4_palette[3U]);

  rendererOamSetTileIdx(0U, 1U);
  rendererOamSetTileIdx(1U, 2U);
  rendererOamSetTileIdx(2U, 3U);
  rendererOamSetTileIdx(3U, 4U);
  rendererOamSetPalleteIdx(0U, 0U);
  rendererOamSetPalleteIdx(1U, 1U);
  rendererOamSetPalleteIdx(2U, 2U);
  rendererOamSetPalleteIdx(3U, 3U);
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
