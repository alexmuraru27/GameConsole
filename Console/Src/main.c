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
static uint8_t framecount = 0U;
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
    1, 1, 3, 2, 1, 1, 1, 1,
    1, 1, 2, 2, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1);

const uint8_t sprite_pacman_big_2[16U] = DEFINE_TILE(
    1, 1, 1, 1, 1, 1, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 2, 1, 1,
    1, 1, 1, 1, 2, 3, 1, 1,
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
  static uint32_t lastFrameTime = 0U;
  if (getSysTime() - lastFrameTime < FRAME_PERIOD)
  {
    // Busy-wait until it's time for the next frame
    while ((getSysTime() - lastFrameTime) < FRAME_PERIOD)
      ;
  }
  lastFrameTime = getSysTime(); // Keep consistent frame timing
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
  ili9341FillRectangle(x, y, TILE_SIZE, TILE_SIZE, ILI9341_RED);
}

int main(void)
{
  consoleInit();
  rendererPatternTableSetTile(0U, sprite_pacman, sizeof(sprite_pacman));
  rendererPatternTableSetTile(1U, sprite_lines, sizeof(sprite_lines));

  rendererPatternTableSetTile(2U, sprite_pacman_big_1, sizeof(sprite_pacman_big_1));
  rendererPatternTableSetTile(3U, sprite_pacman_big_2, sizeof(sprite_pacman_big_2));
  rendererPatternTableSetTile(4U, sprite_pacman_big_3, sizeof(sprite_pacman_big_3));
  rendererPatternTableSetTile(5U, sprite_pacman_big_4, sizeof(sprite_pacman_big_4));

  rendererPaletteSetSprite(0U, 1U, 0x1C);
  rendererPaletteSetSprite(0U, 2U, 0x2C);
  rendererPaletteSetSprite(0U, 3U, 0x0C);

  rendererPaletteSetSprite(1U, 1U, 0x1A);
  rendererPaletteSetSprite(1U, 2U, 0x2A);
  rendererPaletteSetSprite(1U, 3U, 0x0A);

  rendererPaletteSetSprite(2U, 1U, 0x16);
  rendererPaletteSetSprite(2U, 2U, 0x26);
  rendererPaletteSetSprite(2U, 3U, 0x06);

  rendererPaletteSetSprite(3U, 1U, 0x17);
  rendererPaletteSetSprite(3U, 2U, 0x27);
  rendererPaletteSetSprite(3U, 3U, 0x37);

  while (1)
  {
    update();
    render();

    syncFrame();
    framecount++;
  }
}
