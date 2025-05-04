#ifndef __ILI9341_H
#define __ILI9341_H
#include <stdint.h>

void ILI9341Init(void);

void fillScreen(uint16_t colour);
void drawPixel(uint16_t x, uint16_t y, uint16_t colour);
void setDisplayRotation(uint8_t rotation);
#endif /* __ILI9341_H */