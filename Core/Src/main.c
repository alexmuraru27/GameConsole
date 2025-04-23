#include "sysclock.h"
#include <stm32f407xx.h>

void SystemInit(void)
{
  systemClockConfig();
}

int main(void)
{
  // Set PA6 as output
  GPIOA->MODER &= ~(0x3 << (6 * 2));
  GPIOA->MODER |= (0x1 << (6 * 2));

  // Set PA6 output type as push-pull (default)
  GPIOA->OTYPER &= ~(1 << 6);

  // Set PA6 speed to high (optional)
  GPIOA->OSPEEDR |= (0x3 << (6 * 2));

  // Disable pull-up/pull-down for PA6
  GPIOA->PUPDR &= ~(0x3 << (6 * 2));
  while (1)
  {
    GPIOA->ODR ^= GPIO_ODR_OD6;
    delayMs(500);
  }
}
