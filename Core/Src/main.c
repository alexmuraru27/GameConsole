#include "sysclock.h"
#include <stm32f407xx.h>
#include "usart.h"
#include "gpio.h"
#include "dma.h"

void SystemInit(void)
{
  systemClockConfig();
  dmaInit();
  gpioInit();
  usartInit();
}

int main(void)
{
  while (1)
  {
    GPIOA->ODR ^= GPIO_ODR_OD6;
    debugString("TestStringData \r\n");
    debugInt(118932);
    debugString("\r\n");
    debugHex(0x0120340);
    debugString("\r\n");
    debugString("Temperature: 12345678910 12345678910 12345678910 12345678910\r\n");
    debugString("Data: 0x00000000 0x1111111111 0x22222222 0x33333333 0x44444444\r\n");
    debugString("Some random text I would love to write here that I hope will totally work fine\r\n");
    debugString("But some times there is a small chance that it doesnt work as fine as I would wish\r\n");
    delay(500U);
  }
}
