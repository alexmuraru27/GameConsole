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
#include "tileCreator.h"

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
const uint16_t TILE_SIZE = 8U;
const uint16_t SPEED = 5U;

const uint8_t sprite_pacman[RENDERER_TILE_MEMORY_SIZE] = DEFINE_TILE_16(
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2, 1, 1, 1, 1, 3, 3, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2, 1, 1, 1, 1, 3, 3, 0, 0, 0, 0,
    0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 3, 3, 0, 0,
    0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 3, 3, 0, 0,
    0, 0, 2, 2, 3, 3, 1, 1, 3, 3, 1, 1, 3, 3, 0, 0,
    0, 0, 2, 2, 3, 3, 1, 1, 3, 3, 1, 1, 3, 3, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 0, 0,
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 3, 3, 0, 0,
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 3, 3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

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
  if (x <= RENDERER_WIDTH - TILE_SIZE - SPEED)
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x >= 0U + SPEED)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y <= RENDERER_HEIGHT - TILE_SIZE - SPEED)
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
  rendererPatternTableSetTile(1U, sprite_pacman, sizeof(sprite_pacman));

  rendererPaletteSetSpriteMultiple(0U, 0x1C, 0x2C, 0x0C);
  rendererPaletteSetSpriteMultiple(1U, 0x1A, 0x2A, 0x0A);
  rendererPaletteSetSpriteMultiple(2U, 0x16, 0x26, 0x06);
  rendererPaletteSetSpriteMultiple(3U, 0x17, 0x27, 0x07);

  rendererOamSetTileIdx(0U, 1U);
  rendererOamSetPalleteIdx(0U, 2U);
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
