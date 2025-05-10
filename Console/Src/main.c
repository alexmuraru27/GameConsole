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

static void debugJoystics()
{
  debugString("\r\n");
  debugString("\r\n");
  debugString("RBtnUp= ");
  debugInt((uint8_t)joystickGetRBtnUp());
  debugString("\r\n");
  debugString("RBtnRight= ");
  debugInt((uint8_t)joystickGetRBtnRight());
  debugString("\r\n");
  debugString("RBtnDown= ");
  debugInt((uint8_t)joystickGetRBtnDown());
  debugString("\r\n");
  debugString("RBtnLeft= ");
  debugInt((uint8_t)joystickGetRBtnLeft());
  debugString("\r\n");
  debugString("LBtnUp= ");
  debugInt((uint8_t)joystickGetLBtnUp());
  debugString("\r\n");
  debugString("LBtnRight= ");
  debugInt((uint8_t)joystickGetLBtnRight());
  debugString("\r\n");
  debugString("LBtnDown= ");
  debugInt((uint8_t)joystickGetLBtnDown());
  debugString("\r\n");
  debugString("LBtnLeft= ");
  debugInt((uint8_t)joystickGetLBtnLeft());
  debugString("\r\n");
  debugString("SpecialBtn1= ");
  debugInt((uint8_t)joystickGetSpecialBtn1());
  debugString("\r\n");
  debugString("SpecialBtn2= ");
  debugInt((uint8_t)joystickGetSpecialBtn2());
  debugString("\r\n");
  debugString("RAnalogY= ");
  debugInt((uint8_t)joystickGetRAnalogY());
  debugString("\r\n");
  debugString("RAnalogX= ");
  debugInt((uint8_t)joystickGetRAnalogX());
  debugString("\r\n");
  debugString("LAnalogY= ");
  debugInt((uint8_t)joystickGetLAnalogY());
  debugString("\r\n");
  debugString("LAnalogX= ");
  debugInt((uint8_t)joystickGetLAnalogX());
  debugString("\r\n");
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
      x += (uint16_t)joystickGetRBtnRight() * SPEED;
    }
    if (x > 0U)
    {
      x -= (uint16_t)joystickGetRBtnLeft() * SPEED;
    }

    if (y < 240U - TILE_SIZE - SPEED)
    {
      y += (uint16_t)joystickGetRBtnDown() * SPEED;
    }
    if (y > 0U)
    {
      y -= (uint16_t)joystickGetRBtnUp() * SPEED;
    }

    ili9341FillRectangle(x, y, TILE_SIZE, TILE_SIZE, ILI9341_RED);
    // Wait until 33ms have passed since last frame

    // 33ms frame time
    if (getSysTime() - lastFrameTime < 33)
    {
      // Busy-wait until it's time for the next frame
      while ((getSysTime() - lastFrameTime) < 33)
        ;
    }
    lastFrameTime = getSysTime(); // Keep consistent frame timing
    debugJoystics();
    framecount++;
  }
}
