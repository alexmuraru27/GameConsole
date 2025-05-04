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

int main(void)
{
  peripheralsInit();
  uint16_t color = 0xABCD;
  while (1)
  {
    // debugString("TestStringData \r\n");
    // debugInt(118932);
    // debugString("\r\n");
    // debugHex(0x0120340);
    // debugString("\r\n");
    // debugString("Temperature: 12345678910 12345678910 12345678910 12345678910\r\n");
    // debugString("Data: 0x00000000 0x1111111111 0x22222222 0x33333333 0x44444444\r\n");
    // debugString("Some random text I would love to write here that I hope will totally work fine\r\n");
    // debugString("But some times there is a small chance that it doesnt work as fine as I would wish\r\n");
    // drawPixel(50, 50, 0xEEEE);
    // drawPixel(50, 51, 0xEEEE);
    // drawPixel(50, 53, 0xEEEE);
    // drawPixel(50, 54, 0xEEEE);
    // drawPixel(50, 55, 0xEEEE);
    fillScreen(color);
    color += 10U;
    delay(1000U);
  }
}
