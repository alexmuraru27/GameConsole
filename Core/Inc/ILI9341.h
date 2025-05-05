#ifndef __ILI9341_H
#define __ILI9341_H
#include <stdint.h>

#define ILI9341_BLACK 0x0000
#define ILI9341_BLUE 0x001F
#define ILI9341_RED 0xF800
#define ILI9341_GREEN 0x07E0
#define ILI9341_CYAN 0x07FF
#define ILI9341_MAGENTA 0xF81F
#define ILI9341_YELLOW 0xFFE0
#define ILI9341_WHITE 0xFFFF

void ili9341Init(uint8_t rotation);
void ili9341SetDisplayRotation(uint8_t rotation);

void ili9341FillScreen(uint16_t colour);
void ili9341DrawPixel(uint16_t x, uint16_t y, uint16_t colour);
void ili9341FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
#endif /* __ILI9341_H */