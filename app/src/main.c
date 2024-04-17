#include "stm32f407xx.h"
#include "clock/app_clock_config.h"
#include "shared/include/joystick_driver/joystick.h"
#include "shared/include/serial_debug/serial_debug.h"
#include "shared/include/sys_clock_config/sys_clock_config.h"
#include "shared/include/spi_lcd_driver/ST7735.h"

#define FPS 60U
#define MILLISECONDS_IN_SECOND 1000U
#define FRAME_TIME (MILLISECONDS_IN_SECOND / FPS)
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

int main(void)
{
    __enable_irq();
    hw_init();

    uint8_t y = 0U;
    uint8_t x = 0U;

    uint32_t delta_time = get_sys_time() + FRAME_TIME;
    uint32_t start_time = 0U;
    uint32_t end_time = 0U;
    while (1)
    {
        start_time = get_sys_time();
        GPIOA->BSRR |= (1 << 6) << 16;
        while (delta_time > get_sys_time())
        {
        }
        delta_time = get_sys_time() + FRAME_TIME;
        GPIOA->BSRR |= (1 << 6);

        JOYSTICK_SWITCH sw = get_joystick_switch();
        JOYSTICK_HORIZONTAL jh = get_joystick_x();
        JOYSTICK_VERTICAL jv = get_joystick_y();

        if (jv == JOYSTICK_VERTICAL_UP && y > 0)
        {
            y--;
        }
        if (jv == JOYSTICK_VERTICAL_DOWN && y < 127)
        {
            y++;
        }

        if (jh == JOYSTICK_HORIZONTAL_LEFT && x > 0)
        {
            x--;
        }
        if (jh == JOYSTICK_HORIZONTAL_RIGHT && x < 127)
        {
            x++;
        }

        if (sw == JOYSTICK_SWITCH_ON)
        {
            ST7735_FillScreen(ST7735_WHITE);
            ST7735_DrawString(15, 5, "Git gud123", Font_11x18, ST7735_BLUE, ST7735_WHITE);
        }

        ST7735_DrawPixel(x, y, ST7735_BLUE);
        end_time = get_sys_time();
        ST7735_DrawNumber(50, 50, (1000U / (end_time - start_time)), Font_11x18, ST7735_BLUE, ST7735_WHITE);
    }
}