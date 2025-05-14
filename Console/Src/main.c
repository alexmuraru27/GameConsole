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
static uint32_t lastFrameTime = 0U;
const uint16_t TILE_SIZE = 8U;
const uint16_t SPEED = 5U;

// pixel color index = (bit[1] << 1) | (bit[0])
// 0 transparent
// 1 mid
// 2 bright
// 3 dark
const uint8_t sprite_pacman[16U] = {
    0b00000000, 0b00011100, 0b00010110, 0b00111110, 0b01111110, 0b01111110, 0b01010110, 0b00000000, // Plane 0
    0b00000000, 0b00100100, 0b01000010, 0b01101010, 0b00000010, 0b00000010, 0b00000010, 0b00000000  // Plane 1
};

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
    ili9341FillScreen(ILI9341_GREENYELLOW);
  }
  rendererRender();
  ili9341FillRectangle(x, y, TILE_SIZE, TILE_SIZE, ILI9341_RED);
}

int main(void)
{
  consoleInit();
  ili9341FillScreen(ILI9341_GREENYELLOW);
  while (1)
  {
    update();
    render();

    // 33ms frame time
    if (getSysTime() - lastFrameTime < 33U)
    {
      // Busy-wait until it's time for the next frame
      while ((getSysTime() - lastFrameTime) < 33U)
        ;
    }
    lastFrameTime = getSysTime(); // Keep consistent frame timing
    framecount++;
  }
}
