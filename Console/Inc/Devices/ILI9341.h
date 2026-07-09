#ifndef __ILI9341_H
#define __ILI9341_H
#include <stdint.h>

#define FSMC_DATA_ADDRESS ((volatile uint16_t *)0x60020000U)

#define ILI9341_WIDTH 320
#define ILI9341_HEIGHT 240

#define ILI9341_BLACK 0x0000
#define ILI9341_NAVY 0x000F
#define ILI9341_DARKGREEN 0x03E0
#define ILI9341_DARKCYAN 0x03EF
#define ILI9341_MAROON 0x7800
#define ILI9341_PURPLE 0x780F
#define ILI9341_OLIVE 0x7BE0
#define ILI9341_LIGHTGREY 0xC618
#define ILI9341_DARKGREY 0x7BEF
#define ILI9341_BLUE 0x001F
#define ILI9341_GREEN 0x07E0
#define ILI9341_CYAN 0x07FF
#define ILI9341_RED 0xF800
#define ILI9341_MAGENTA 0xF81F
#define ILI9341_YELLOW 0xFFE0
#define ILI9341_WHITE 0xFFFF
#define ILI9341_ORANGE 0xFD20
#define ILI9341_GREENYELLOW 0xAFE5
#define ILI9341_PINK 0xFC18

void ili9341Init(uint8_t rotation, uint16_t window_width, uint16_t window_height);
void ili9341FillScreen(uint16_t color);
void ili9341DrawScanlines(uint8_t lines, uint16_t lines_offset, uint16_t array_size, const uint16_t *data);

#endif /* __ILI9341_H */