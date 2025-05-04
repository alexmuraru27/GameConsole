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
  ILI9341Init();
  adcInit();
  joystickInit();
}

static void debugJoystics()
{
  debugString("\r\n");
  joystickReadData();
  debugString("\r\n");
  debugString("joystickGetRBtnUp= ");
  debugInt((uint8_t)joystickGetRBtnUp());
  debugString("\r\n");
  debugString("joystickGetRBtnRight= ");
  debugInt((uint8_t)joystickGetRBtnRight());
  debugString("\r\n");
  debugString("joystickGetRBtnDown= ");
  debugInt((uint8_t)joystickGetRBtnDown());
  debugString("\r\n");
  debugString("joystickGetRBtnLeft= ");
  debugInt((uint8_t)joystickGetRBtnLeft());
  debugString("\r\n");
  debugString("joystickGetLBtnUp= ");
  debugInt((uint8_t)joystickGetLBtnUp());
  debugString("\r\n");
  debugString("joystickGetLBtnRight= ");
  debugInt((uint8_t)joystickGetLBtnRight());
  debugString("\r\n");
  debugString("joystickGetLBtnDown= ");
  debugInt((uint8_t)joystickGetLBtnDown());
  debugString("\r\n");
  debugString("joystickGetLBtnLeft= ");
  debugInt((uint8_t)joystickGetLBtnLeft());
  debugString("\r\n");
  debugString("joystickGetSpecialBtn1= ");
  debugInt((uint8_t)joystickGetSpecialBtn1());
  debugString("\r\n");
  debugString("joystickGetSpecialBtn2= ");
  debugInt((uint8_t)joystickGetSpecialBtn2());
  debugString("\r\n");
  debugString("joystickGetRAnalogY= ");
  debugInt((uint8_t)joystickGetRAnalogY());
  debugString("\r\n");
  debugString("joystickGetRAnalogX= ");
  debugInt((uint8_t)joystickGetRAnalogX());
  debugString("\r\n");
  debugString("joystickGetLAnalogY= ");
  debugInt((uint8_t)joystickGetLAnalogY());
  debugString("\r\n");
  debugString("joystickGetLAnalogX= ");
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
  drawPixel(50, 50, 0xEEEE);
  drawPixel(50, 51, 0xEEEE);
  drawPixel(50, 53, 0xEEEE);
  drawPixel(50, 54, 0xEEEE);
  drawPixel(50, 55, 0xEEEE);
  fillScreen(0xABCD);
}
int main(void)
{
  peripheralsInit();
  while (1)
  {
    // debugJoystics();
    // debugUsart2();
    // debugSpiDisplay();
    delay(1000U);
  }
}
