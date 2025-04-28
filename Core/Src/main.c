#include "sysclock.h"
#include <stm32f407xx.h>
#include "usart.h"
#include "gpio.h"
#include "dma.h"

void SystemInit(void)
{
  systemClockConfig();
  gpioInit();
  usartInit();
  dmaInit();
}

int main(void)
{
  while (1)
  {
    GPIOA->ODR ^= GPIO_ODR_OD6;
    usart2SendString("TestStringData \r\n");
    usart2SendInt(118932);
    usart2SendString("\r\n");
    usart2SendHex(0x0120340);
    delay(500U);
  }
}
