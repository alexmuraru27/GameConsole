#include "stm32f407xx.h"
#include "clock/app_clock_config.h"
#include "shared/include/joystick_driver/joystick.h"
#include "shared/include/serial_debug/serial_debug.h"
#include "shared/include/sys_clock_config/sys_clock_config.h"
#include "shared/include/spi_lcd_driver/ST7735.h"

// Serial
// PC10 - TX

// Display
// PB09 AF5 - CS SPI2
// PB10 AF5 - CLK SPI2
// PB15 AF5 - SDI SPI2
// PB11 - RS(DATA/COMMAND)
// PB12 - RST

// Joystick
// PA2 SW
// PA1 VRY
// PA0 VRX

void gpio_config(void)
{
    GPIOA->MODER |= (1 << GPIO_MODER_MODER6_Pos);
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED6;
    GPIOA->OSPEEDR |= (3 << GPIO_OSPEEDR_OSPEED6_Pos);
}

void hw_init(void)
{
    app_clock_config();
    gpio_config();
    joystick_init();
    serial_init();
    ST7735_Init();
}

// void ST7735_Init(void);
// void ST7735_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
// void ST7735_DrawString(uint16_t x, uint16_t y, const char *str, FontDef font, uint16_t color, uint16_t bgcolor);
// void ST7735_FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
// void ST7735_FillScreen(uint16_t color);
// void ST7735_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
// void ST7735_DrawTouchGFX(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);
// void ST7735_InvertColors(uint8_t invert);
// void ST7735_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
// void ST7735_DrawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color);
// void ST7735_FillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
// void ST7735_FillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, uint16_t color);
// void ST7735_DrawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
// void ST7735_FillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
// void ST7735_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
// void ST7735_DrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
// void ST7735_FillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
// void ST7735_DrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
// void ST7735_FillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
// void ST7735_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
// void ST7735_DrawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
// void ST7735_DrawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
// void ST7735_SetRotation(uint8_t m);

int main(void)
{
    __enable_irq();
    hw_init();

    uint8_t y = 0U;
    uint8_t x = 0U;

    JOYSTICK_SWITCH sw = get_joystick_switch();
    uint32_t i = 0;

    while (1)
    {
        if (get_joystick_y() == JOYSTICK_VERTICAL_UP && y > 0)
        {
            y--;
        }
        if (get_joystick_y() == JOYSTICK_VERTICAL_DOWN && y < 127)
        {
            y++;
        }

        if (get_joystick_x() == JOYSTICK_HORIZONTAL_LEFT && x > 0)
        {
            x--;
        }
        if (get_joystick_x() == JOYSTICK_HORIZONTAL_RIGHT && x < 127)
        {
            x++;
        }

        sw = get_joystick_switch();
        GPIOA->BSRR |= (1 << 6);
        delay_ms_systick(10);

        if (sw == JOYSTICK_SWITCH_ON)
        {
            ST7735_FillScreen(ST7735_WHITE);
            ST7735_DrawString(15, 5, "Git gud", Font_11x18, ST7735_BLUE, ST7735_WHITE);
        }

        ST7735_DrawCircle(x, y, 20, ST7735_BLUE);
        // ON
        GPIOA->BSRR |= (1 << 6) << 16;
    }
}