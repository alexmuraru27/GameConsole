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
  ili9341Init(3U);
  adcInit();
  joystickInit();
}

static void consoleInit()
{
  peripheralsInit();
  rendererInit();
}

// TODO Remove statics :)
static uint16_t x = 0U;
static uint16_t y = 0U;
const uint16_t TILE_SIZE = 8U;
const uint16_t SPEED = 5U;

const uint8_t sprite_pacman[16U] = DEFINE_TILE(
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 1, 1, 3, 0, 0,
    0, 2, 0, 1, 0, 1, 3, 0,
    0, 2, 3, 1, 3, 1, 3, 0,
    0, 1, 1, 1, 1, 1, 3, 0,
    0, 1, 1, 1, 1, 1, 3, 0,
    0, 1, 0, 1, 0, 1, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 0);

const uint8_t sprite_lines[16U] = DEFINE_TILE(
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3);

const uint8_t sprite_pacman_big_1[16U] = DEFINE_TILE(
    0, 0, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 2, 3, 1, 1, 1, 1,
    1, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1);

const uint8_t sprite_pacman_big_2[16U] = DEFINE_TILE(
    1, 1, 1, 1, 1, 1, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 1,
    1, 1, 1, 1, 3, 2, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1);

const uint8_t sprite_pacman_big_3[16U] = DEFINE_TILE(
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 1, 0, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 1, 0, 1, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0);

const uint8_t sprite_pacman_big_4[16U] = DEFINE_TILE(
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0);

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
  if (x < 320U - TILE_SIZE - SPEED)
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x > 0U)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y < 240U - TILE_SIZE - SPEED)
  {
    y += ((joystickGetLAnalogY() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogY() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (y > 0U)
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
    ili9341FillScreen(ILI9341_BLACK);
  }
  if (joystickGetSpecialBtn2())
  {
    ili9341FillScreen(ILI9341_WHITE);
  }

  rendererRender();
}

static void screenInit()
{
  rendererPatternTableSetTile(1U, sprite_pacman, sizeof(sprite_pacman));
  rendererPatternTableSetTile(2U, sprite_lines, sizeof(sprite_lines));

  rendererPatternTableSetTile(3U, sprite_pacman_big_1, sizeof(sprite_pacman_big_1));
  rendererPatternTableSetTile(4U, sprite_pacman_big_2, sizeof(sprite_pacman_big_2));
  rendererPatternTableSetTile(5U, sprite_pacman_big_3, sizeof(sprite_pacman_big_3));
  rendererPatternTableSetTile(6U, sprite_pacman_big_4, sizeof(sprite_pacman_big_4));

  rendererPaletteSetSpriteMultiple(0U, 0x1C, 0x2C, 0x0C);
  rendererPaletteSetSpriteMultiple(1U, 0x1A, 0x2A, 0x0A);
  rendererPaletteSetSpriteMultiple(2U, 0x16, 0x26, 0x06);
  rendererPaletteSetSpriteMultiple(3U, 0x17, 0x27, 0x07);

  rendererOamSetTileIdx(0U, 1U);
  rendererOamSetPalleteIdx(0U, 2U);
  uint8_t big_sprite_palette = 0U;
  rendererOamSetTileIdx(1U, 3U);
  rendererOamSetXPos(1U, 100U);
  rendererOamSetYPos(1U, 100U);
  rendererOamSetPalleteIdx(1U, big_sprite_palette);

  rendererOamSetTileIdx(2U, 4U);
  rendererOamSetXPos(2U, 108U);
  rendererOamSetYPos(2U, 100U);
  rendererOamSetPalleteIdx(2U, big_sprite_palette);

  rendererOamSetTileIdx(3U, 5U);
  rendererOamSetXPos(3U, 100U);
  rendererOamSetYPos(3U, 108U);
  rendererOamSetPalleteIdx(3U, big_sprite_palette);

  rendererOamSetTileIdx(4U, 6U);
  rendererOamSetXPos(4U, 108U);
  rendererOamSetYPos(4U, 108U);
  rendererOamSetPalleteIdx(4U, big_sprite_palette);
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
