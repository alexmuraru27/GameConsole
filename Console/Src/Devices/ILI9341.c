#include "ILI9341.h"
#include "spi.h"
#include "stm32f407xx.h"
#include "sysclock.h"
#include "stdbool.h"
#include "gpio.h"

static uint16_t s_screen_width;  // Display width as modified by current rotation
static uint16_t s_screen_height; // Display height as modified by current rotation
static uint16_t s_window_width;
static uint16_t s_window_height;
static uint16_t s_window_x_offset;
static uint16_t s_window_y_offset;
#define ILI9341_SLPOUT 0x11            // Sleep Out
#define ILI9341_GAMMASET 0x26          // Gamma Set
#define ILI9341_DISPON 0x29            // Display ON
#define ILI9341_CASET 0x2A             // Column Address Set
#define ILI9341_PASET 0x2B             // Page Address Set
#define ILI9341_RAMWR 0x2C             // Memory Write
#define ILI9341_MADCTL 0x36            // Memory Access Control
#define ILI9341_VSCRSADD 0x37          // Vertical Scrolling Start Address
#define ILI9341_PIXFMT 0x3A            // COLMOD: Pixel Format Set
#define ILI9341_FRMCTR1 0xB1           // Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_DFUNCTR 0xB6           // Display Function Control
#define ILI9341_PWCTR1 0xC0            // Power Control 1
#define ILI9341_PWCTR2 0xC1            // Power Control 2
#define ILI9341_PWCTRA 0xCB            // Power Control A
#define ILI9341_PWCTRB 0xCF            // Power Control B
#define ILI9341_VMCTR1 0xC5            // VCOM Control 1
#define ILI9341_VMCTR2 0xC7            // VCOM Control 2
#define ILI9341_GMCTRP1 0xE0           // Positive Gamma Correction
#define ILI9341_GMCTRN1 0xE1           // Negative Gamma Correction
#define ILI9341_3G 0xF2                // Negative Gamma Correction
#define ILI9341_DRIVERTIMINGCTR_B 0xEA // Driver Timing control B
#define ILI9341_DRIVERTIMINGCTR_A 0xE8 // Driver Timing control A
#define ILI9341_PUMP_RATIO 0xF7
#define ILI9341_PWRON_SEQ_CTR 0xED
#define MADCTL_MY 0x80 // Bottom to top
#define MADCTL_MX 0x40 // Right to left
#define MADCTL_MV 0x20 // Reverse Mode
#define MADCTL_BGR 0x08

static void ili9341Reset()
{
    gpioSpi1RstLow();
    delay(1);
    gpioSpi1RstHigh();
}

static void setDCtoCommandMode()
{
    while (SPI1->SR & SPI_SR_BSY)
        ;
    gpioSpi1DcLow();
}

static void setDCtoDataMode()
{
    while (SPI1->SR & SPI_SR_BSY)
        ;
    gpioSpi1DcHigh();
}

static void ili9341WriteCommand(uint8_t cmd)
{
    setDCtoCommandMode();
    spiWrite(cmd);
}

static void ili9341WriteDataBuffer(uint8_t *buff, uint32_t buff_size)
{
    for (uint32_t i = 0U; i < buff_size; ++i)
    {
        spiWrite(buff[i]);
    }
}

static void ili9341WriteData(uint8_t data)
{
    spiWrite(data);
}

static void ili9341SetAddrWindowScreen(const uint16_t x1, const uint16_t y1, const uint16_t w,
                                       const uint16_t h)
{
    const uint16_t x2 = (x1 + w - 1);
    const uint16_t y2 = (y1 + h - 1);

    // set column
    ili9341WriteCommand(ILI9341_CASET);
    setDCtoDataMode();
    ili9341WriteData(x1 >> 8U);
    ili9341WriteData(x1);
    ili9341WriteData(x2 >> 8U);
    ili9341WriteData(x2);

    // set row
    ili9341WriteCommand(ILI9341_PASET);
    setDCtoDataMode();
    ili9341WriteData(y1 >> 8U);
    ili9341WriteData(y1);
    ili9341WriteData(y2 >> 8U);
    ili9341WriteData(y2);

    // set new window to spi slave ram
    ili9341WriteCommand(ILI9341_RAMWR);
}

static void ili9341SetDisplayRotation(uint8_t rotation, uint16_t window_width, uint16_t window_height)
{

    switch (rotation % 4)
    {
    case 0:
        rotation = (MADCTL_MX | MADCTL_BGR);
        s_screen_width = ILI9341_HEIGHT;
        s_screen_height = ILI9341_WIDTH;
        s_window_width = window_height;
        s_window_height = window_width;
        break;
    case 1:
        rotation = (MADCTL_MV | MADCTL_BGR);
        s_screen_width = ILI9341_WIDTH;
        s_screen_height = ILI9341_HEIGHT;
        s_window_width = window_width;
        s_window_height = window_height;
        break;
    case 2:
        rotation = (MADCTL_MY | MADCTL_BGR);
        s_screen_width = ILI9341_HEIGHT;
        s_screen_height = ILI9341_WIDTH;
        s_window_width = window_height;
        s_window_height = window_width;
        break;
    case 3:
        rotation = (MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
        s_screen_width = ILI9341_WIDTH;
        s_screen_height = ILI9341_HEIGHT;
        s_window_width = window_width;
        s_window_height = window_height;
        break;
    }
    ili9341WriteCommand(ILI9341_MADCTL);
    setDCtoDataMode();
    ili9341WriteData(rotation);

    s_window_x_offset = (s_screen_width - window_width) / 2;
    s_window_y_offset = (s_screen_height - window_height) / 2;
}

void ili9341FillScreen(uint16_t color)
{
    ili9341SetAddrWindowScreen(0, 0, s_screen_width, s_screen_height);
    setDCtoDataMode();
    for (uint16_t y = s_screen_height; y > 0; y--)
    {
        for (uint16_t x = s_screen_width; x > 0; x--)
        {
            ili9341WriteData(color >> 8);
            ili9341WriteData(color & 0xFF);
        }
    }
}

void ili9341Init(uint8_t rotation, uint16_t window_width, uint16_t window_height)
{
    // select display
    gpioSpi1CsLow();

    // init SPI
    spiInit();

    // HW reset
    ili9341Reset();

    // SW RESET
    ili9341WriteCommand(0x01);
    delay(1U);

    {
        ili9341WriteCommand(ILI9341_PWCTRA);
        uint8_t data[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_PWCTRB);
        uint8_t data[] = {0x00, 0xC1, 0x30};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_DRIVERTIMINGCTR_A);
        uint8_t data[] = {0x85, 0x00, 0x78};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_DRIVERTIMINGCTR_B);
        uint8_t data[] = {0x00, 0x00};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_PWRON_SEQ_CTR);
        uint8_t data[] = {0x64, 0x03, 0x12, 0x81};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_PUMP_RATIO);
        uint8_t data[] = {0x20};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_PWCTR1);
        uint8_t data[] = {0x23};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_PWCTR2);
        uint8_t data[] = {0x10};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_VMCTR1);
        uint8_t data[] = {0x3E, 0x28};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_VMCTR2);
        uint8_t data[] = {0x86};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_MADCTL);
        uint8_t data[] = {0x48};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_PIXFMT);
        uint8_t data[] = {0x55};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_FRMCTR1);
        uint8_t data[] = {0x00, 0x18};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_DFUNCTR);
        uint8_t data[] = {0x08, 0x82, 0x27};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_3G);
        uint8_t data[] = {0x00};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_GAMMASET);
        uint8_t data[] = {0x01};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_GMCTRP1);
        uint8_t data[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                          0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    {
        ili9341WriteCommand(ILI9341_GMCTRN1);
        uint8_t data[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                          0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
        setDCtoDataMode();
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    // Delay 5ms before as per catalog
    delay(5);
    ili9341WriteCommand(ILI9341_SLPOUT);
    // Delay 120ms after as per catalog
    delay(120);
    ili9341WriteCommand(ILI9341_DISPON);
    ili9341SetDisplayRotation(rotation, window_width, window_height);

    ili9341FillScreen(ILI9341_BLACK);
}

void ili9341DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= s_window_width) || (y >= s_window_height))
        return;

    ili9341SetAddrWindowScreen(x + s_window_x_offset, y + s_window_y_offset, 1, 1);
    uint8_t data[] = {color >> 8, color & 0xFF};
    setDCtoDataMode();
    ili9341WriteDataBuffer(data, sizeof(data));
}

void ili9341SetAddrWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h)
{
    ili9341SetAddrWindowScreen(x + s_window_x_offset, y + s_window_y_offset, w, h);
}

void ili9341SendPixel(uint16_t color)
{
    setDCtoDataMode();
    uint8_t data[] = {color >> 8, color & 0xFF};
    ili9341WriteDataBuffer(data, sizeof(data));
}

void ili9341FillWindow(uint16_t color)
{
    ili9341SetAddrWindowScreen(s_window_x_offset, s_window_y_offset, s_window_width, s_window_height);
    setDCtoDataMode();
    for (uint16_t y = s_window_width; y > 0; y--)
    {
        for (uint16_t x = s_window_height; x > 0; x--)
        {
            ili9341WriteData(color >> 8);
            ili9341WriteData(color & 0xFF);
        }
    }
}

void ili9341DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if ((x >= s_window_width) || (y >= s_window_height))
        return;
    if ((x + w - 1) >= s_window_width)
        return;
    if ((y + h - 1) >= s_window_height)
        return;

    ili9341SetAddrWindowScreen(x + s_window_x_offset, y + s_window_y_offset, w, h);
    setDCtoDataMode();
    ili9341WriteDataBuffer((uint8_t *)data, sizeof(uint16_t) * w * h);
}

void ili9341FillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((x >= s_window_width) || (y >= s_window_height))
        return;
    if ((x + w - 1) >= s_window_width)
        w = s_window_width - x;
    if ((y + h - 1) >= s_window_height)
        h = s_window_height - y;

    ili9341SetAddrWindowScreen(x + s_window_x_offset, y + s_window_x_offset, w, h);
    setDCtoDataMode();
    for (y = h; y > 0; y--)
    {
        for (x = w; x > 0; x--)
        {
            ili9341WriteData(color >> 8);
            ili9341WriteData(color & 0xFF);
        }
    }
}