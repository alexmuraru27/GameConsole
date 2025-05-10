#include "sysclock.h"
#include <stm32f407xx.h>
#include "usart.h"
#include "gpio.h"
#include "dma.h"
#include "ILI9341.h"
#include "adc.h"
#include "timer.h"
#include "joystick.h"

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

static void debugUsart2()
{
  debugString("TestStringData \r\n");
  debugInt(118932);
  debugString("\r\n");
  debugHex(0x0120340);
  debugString("\r\n");
  debugString("Temperature: 12345678910 12345678910 12345678910 12345678910\r\n");
  debugString("Data: 0x00000000 0x1111111111 0x22222222 0x33333333 0x44444444\r\n");
  debugString("Some random text I would love to write here that I hope will totally work fine\r\n");
  debugString("But some times there is a small chance that it doesnt work as fine as I would wish\r\n");
  debugString("\r\n");
  debugBinary(0x0A, 8);
  debugString("\r\n");
  debugBinary(0x0A0A, 16);
  debugString("\r\n");
  debugBinary(0x0A0A0A0A, 32);
  debugString("\r\n");
}

static void debugSpiDisplay()
{
  ili9341FillScreen(ILI9341_GREENYELLOW);
  ili9341FillRectangle(16, 16, 50, 50, ILI9341_MAGENTA);
  ili9341FillRectangle(60, 60, 32, 32, ILI9341_CYAN);
  ili9341FillRectangle(150, 120, 40, 40, ILI9341_OLIVE);

  ili9341DrawPixel(180, 180, ILI9341_BLUE);
  ili9341DrawPixel(180, 181, ILI9341_BLUE);
  ili9341DrawPixel(181, 180, ILI9341_BLUE);
  ili9341DrawPixel(181, 181, ILI9341_BLUE);
  delay(1500U);
}
int main(void)
{
  peripheralsInit();
  uint16_t x = 0U;
  uint16_t y = 0U;
  const uint16_t TILE_SIZE = 32U;
  const uint16_t SPEED = 5U;
  uint8_t framecount = 0U;
  uint32_t lastFrameTime = getSysTime();
  while (1)
  {
    if (framecount % 100U == 0)
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
    // debugUsart2();
    // debugSpiDisplay();
    // delay(500U);
    framecount++;
  }
}
