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

int main(void)
{
  consoleInit();
  uint16_t x = 0U;
  uint16_t y = 0U;
  const uint16_t TILE_SIZE = 32U;
  const uint16_t SPEED = 5U;
  uint8_t framecount = 0U;
  uint32_t lastFrameTime = getSysTime();
  while (1)
  {
    if (framecount % 5 == 0)
    {
      ili9341FillScreen(ILI9341_GREENYELLOW);
    }

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

    ili9341FillRectangle(x, y, TILE_SIZE, TILE_SIZE, ILI9341_RED);
    // Wait until 33ms have passed since last frame

    rendererRender();
    // 33ms frame time
    if (getSysTime() - lastFrameTime < 33)
    {
      // Busy-wait until it's time for the next frame
      while ((getSysTime() - lastFrameTime) < 33)
        ;
    }
    lastFrameTime = getSysTime(); // Keep consistent frame timing
    framecount++;
  }
}
