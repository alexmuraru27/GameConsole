#ifndef __GPIO_H
#define __GPIO_H
#include <stdint.h>
#include <stdbool.h>

void gpioInit(void);
bool sdCardPresent(void);
uint16_t gpioReadButtons(void);
#endif /* __GPIO_H */