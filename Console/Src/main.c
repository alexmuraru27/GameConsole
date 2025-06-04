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
#include "buzzer.h"
#include "pacman_ghost.h"
#include "bricks1.h"
#include "bricks2.h"
#include "bricks3.h"
#include "bricks4.h"
#include "bricks5.h"

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
  buzzerInit();
}

static void consoleInit()
{
  peripheralsInit();
  rendererInit();
}

static void syncFrame()
{
  if (getSysTime() - s_last_frame_time < FRAME_PERIOD)
  {
    // Busy-wait until it's time for the next frame
    while ((getSysTime() - s_last_frame_time) < FRAME_PERIOD)
      ;
  }
  debugInt(1000 / (getSysTime() - s_last_frame_time));
  debugString("\r\n");
  s_last_frame_time = getSysTime(); // Keep consistent frame timing
}

static void update()
{
  const uint16_t SPEED = 5U;
  uint8_t x = rendererOamGetXPos(0U);
  uint8_t y = rendererOamGetYPos(0U);
  if (x <= rendererGetSizeWidth() - rendererGetSizeTileScreen())
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x >= 0U + SPEED)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y <= rendererGetSizeHeight() - rendererGetSizeTileScreen())
  {
    y += ((joystickGetLAnalogY() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogY() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (y >= 0U + SPEED)
  {
    y -= ((joystickGetLAnalogY() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogY() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  rendererOamSetXYPos(0U, x, y);
}

uint16_t melody[] = {
    NOTE_E7, NOTE_E7, 0, NOTE_E7,
    0, NOTE_C7, NOTE_E7, 0,
    NOTE_G7, 0, 0, 0,
    NOTE_G6, 0, 0, 0,

    NOTE_C7, 0, 0, NOTE_G6,
    0, 0, NOTE_E6, 0,
    0, NOTE_A6, 0, NOTE_B6,
    0, NOTE_AS6, NOTE_A6, 0,

    NOTE_G6, NOTE_E7, NOTE_G7,
    NOTE_A7, 0, NOTE_F7, NOTE_G7,
    0, NOTE_E7, 0, NOTE_C7,
    NOTE_D7, NOTE_B6, 0, 0,

    NOTE_C7, 0, 0, NOTE_G6,
    0, 0, NOTE_E6, 0,
    0, NOTE_A6, 0, NOTE_B6,
    0, NOTE_AS6, NOTE_A6, 0,

    NOTE_G6, NOTE_E7, NOTE_G7,
    NOTE_A7, 0, NOTE_F7, NOTE_G7,
    0, NOTE_E7, 0, NOTE_C7,
    NOTE_D7, NOTE_B6, 0, 0};

uint16_t tempo[] = {
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    90,
    90,
    90,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    90,
    90,
    90,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
};

uint16_t underworld_melody[] = {
    NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
    NOTE_AS3, NOTE_AS4, 0,
    0,
    NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
    NOTE_AS3, NOTE_AS4, 0,
    0,
    NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
    NOTE_DS3, NOTE_DS4, 0,
    0,
    NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
    NOTE_DS3, NOTE_DS4, 0,
    0, NOTE_DS4, NOTE_CS4, NOTE_D4,
    NOTE_CS4, NOTE_DS4,
    NOTE_DS4, NOTE_GS3,
    NOTE_G3, NOTE_CS4,
    NOTE_C4, NOTE_FS4, NOTE_F4, NOTE_E3, NOTE_AS4, NOTE_A4,
    NOTE_GS4, NOTE_DS4, NOTE_B3,
    NOTE_AS3, NOTE_A3, NOTE_GS3,
    0, 0, 0};

uint16_t underworld_tempo[] = {
    120, 120, 120, 120,
    120, 120, 60,
    30,
    120, 120, 120, 120,
    120, 120, 60,
    30,
    120, 120, 120, 120,
    120, 120, 60,
    30,
    120, 120, 120, 120,
    120, 120, 60,
    60, 180, 180, 180,
    60, 60,
    60, 60,
    60, 60,
    180, 180, 180, 180, 180, 180,
    100, 100, 100,
    100, 100, 100,
    30, 30, 3};

static void render()
{
  if (joystickGetSpecialBtn1())
  {
    ili9341FillWindow(ILI9341_BLACK);
    rendererSetDirtyCompleteRedraw();
    buzzerPlay(0, melody, tempo, sizeof(melody) / sizeof(uint16_t));
  }
  if (joystickGetSpecialBtn2())
  {
    ili9341FillWindow(ILI9341_WHITE);
    rendererSetDirtyCompleteRedraw();
    buzzerPlay(1, underworld_melody, underworld_tempo, sizeof(underworld_melody) / sizeof(uint16_t));
  }

  if (joystickGetRBtnUp())
  {
    buzzerPause(0);
  }
  if (joystickGetRBtnDown())
  {
    buzzerResume(0);
  }
  if (joystickGetRBtnLeft())
  {
    buzzerStop(0);
  }
  rendererRender();
}

static void screenInit()
{
  rendererPatternTableSetTile(1U, pacman_ghost_data, sizeof(pacman_ghost_data));
  rendererFramePaletteSetSpriteMultiple(0U, pacman_ghost_palette[1U], pacman_ghost_palette[2U], pacman_ghost_palette[3U]);
  rendererOamSetTileIdx(0U, 1U);
  rendererOamSetPaletteIdx(0U, 0U);
  rendererOamSetFlipH(0U, true);
  rendererOamSetPriorityLow(0U, true);

  // backgrounds
  rendererPatternTableSetTile(2U, bricks1_data, sizeof(bricks1_data));
  rendererPatternTableSetTile(3U, bricks2_data, sizeof(bricks2_data));
  rendererPatternTableSetTile(4U, bricks3_data, sizeof(bricks3_data));
  rendererPatternTableSetTile(5U, bricks4_data, sizeof(bricks4_data));
  rendererPatternTableSetTile(6U, bricks5_data, sizeof(bricks5_data));

  // background frame palette
  const uint8_t brick_pallete_idx = 1U;
  rendererFramePaletteSetBackgroundMultiple(brick_pallete_idx, bricks1_palette[1U], bricks1_palette[2U], bricks1_palette[3U]);

  // tile [2,0] flipped in both directions
  rendererAttributeTableSetFlipH(2U, 0U, true);
  rendererAttributeTableSetFlipV(2U, 0U, true);

  // background set nametable
  rendererNameTableSetTile(5U, 5U, 2U);
  rendererNameTableSetTile(5U, 5U + 1U, 3U);
  rendererNameTableSetTile(5U, 5U + 2U, 4U);
  rendererNameTableSetTile(5U, 5U + 3U, 5U);
  rendererNameTableSetTile(5U, 5U + 4U, 6U);

  rendererAttributeTableSetPalette(5U, 5U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 1U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 2U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 3U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 4U, brick_pallete_idx);

  for (uint8_t i = 0; i < rendererGetMaxTilesInColumn(); i++)
  {
    rendererNameTableSetTile(0U, i, 4U);
    rendererNameTableSetTile(rendererGetMaxTilesInRow() - 1, i, 4U);

    rendererAttributeTableSetPalette(0U, i, brick_pallete_idx);
    rendererAttributeTableSetPalette(rendererGetMaxTilesInRow() - 1, i, brick_pallete_idx);

    rendererAttributeTableSetPriorityHigh(0U, i, true);
    rendererAttributeTableSetPriorityHigh(rendererGetMaxTilesInRow() - 1, i, true);
  }
  for (uint8_t i = 0; i < rendererGetMaxTilesInRow(); i++)
  {
    rendererNameTableSetTile(i, 0U, 6U);
    rendererNameTableSetTile(i, rendererGetMaxTilesInColumn() - 1, 6U);

    rendererAttributeTableSetPalette(i, 0U, brick_pallete_idx);
    rendererAttributeTableSetPalette(i, rendererGetMaxTilesInColumn() - 1, brick_pallete_idx);

    rendererAttributeTableSetPriorityHigh(i, 0U, false);
    rendererAttributeTableSetPriorityHigh(i, rendererGetMaxTilesInColumn() - 1, true);
  }
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
