#include "sysclock.h"
#include <stm32f407xx.h>
#include "usart.h"
#include "gpio.h"

void SystemInit(void)
{
  systemClockConfig();
  gpioInit();
  usartInit();
}

int main(void)
{
  while (1)
  {
    GPIOA->ODR ^= GPIO_ODR_OD6;
    usart2SendString("TestStringData \r\n");
    usart2SendInt(118932);
    delay(500U);
  }
}
