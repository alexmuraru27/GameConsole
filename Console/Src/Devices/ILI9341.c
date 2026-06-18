#include "ILI9341.h"
#include "stm32f407xx.h"
#include "sysclock.h"
#include "gpio.h"
#include "dma.h"
#include "logger.h"

// FSMC Bank1 Sub-bank1 (NE1, PD7):
//   Command: A16=0 -> CPU address 0x60000000
//   Data:    A16=1 -> CPU address 0x60000000 + (1<<17) = 0x60020000
//   (in 16-bit mode FSMC_A[n] = HADDR[n+1], so A16 = HADDR[17])
#define LCD_CMD_ADDR ((volatile uint16_t *)0x60000000U)
#define LCD_DATA_ADDR (FSMC_DATA_ADDRESS)

#define ili9341WriteCommand(cmd) (*LCD_CMD_ADDR = (uint16_t)(cmd))
#define ili9341WriteData(data) (*LCD_DATA_ADDR = (uint16_t)(data))

static uint16_t s_screen_width;
static uint16_t s_screen_height;
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
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_BGR 0x08

static void fsmcInit(void)
{
    // BCR1: SRAM type, 16-bit bus width, write enabled
    // MTYP=00 (SRAM), MWID=01 (16-bit), MBKEN=1, WREN=1
    FSMC_Bank1->BTCR[0] = FSMC_BCR1_MBKEN | (1U << FSMC_BCR1_MWID_Pos) | FSMC_BCR1_WREN;

    // BTR1: ADDSET=2 HCLK, DATAST=4 HCLK (HCLK=168MHz, ~5.95ns/cycle).
    // WR-high/recovery = (ADDSET+1) ≈ 18ns and WR-low pulse = (DATAST+1) ≈ 30ns,
    // both above the ILI9341's ~15ns per-pulse minimums. tWC = (ADDSET+DATAST+2)
    // ≈ 48ns: under the datasheet's 66ns write-cycle figure but verified
    // artifact-free on this panel, and ~10 FPS faster than a conservative 3/6 (~65ns).
    FSMC_Bank1->BTCR[1] = (2U << FSMC_BTR1_ADDSET_Pos) | (4U << FSMC_BTR1_DATAST_Pos);
}

static void ili9341Reset(void)
{
    GPIOC->BSRR = GPIO_BSRR_BR7; // RST low
    delay(1U);
    GPIOC->BSRR = GPIO_BSRR_BS7; // RST high
}

static void ili9341WriteDataBuffer(const uint8_t *buff, uint32_t buff_size)
{
    for (uint32_t i = 0U; i < buff_size; ++i)
    {
        *LCD_DATA_ADDR = buff[i];
    }
}

static void ili9341SetAddrWindowScreen(const uint16_t x1, const uint16_t y1,
                                       const uint16_t w, const uint16_t h)
{
    const uint16_t x2 = x1 + w - 1U;
    const uint16_t y2 = y1 + h - 1U;
    fsmcDmaWait();
    ili9341WriteCommand(ILI9341_CASET);
    ili9341WriteData(x1 >> 8U);
    ili9341WriteData(x1 & 0xFFU);
    ili9341WriteData(x2 >> 8U);
    ili9341WriteData(x2 & 0xFFU);

    ili9341WriteCommand(ILI9341_PASET);
    ili9341WriteData(y1 >> 8U);
    ili9341WriteData(y1 & 0xFFU);
    ili9341WriteData(y2 >> 8U);
    ili9341WriteData(y2 & 0xFFU);

    ili9341WriteCommand(ILI9341_RAMWR);
}

static void ili9341SetDisplayRotation(uint8_t rotation, uint16_t window_width,
                                      uint16_t window_height)
{
    switch (rotation % 4U)
    {
    case 0:
        rotation = MADCTL_MX | MADCTL_BGR;
        s_screen_width = ILI9341_HEIGHT;
        s_screen_height = ILI9341_WIDTH;
        s_window_width = window_height;
        s_window_height = window_width;
        break;
    case 1:
        rotation = MADCTL_MV | MADCTL_BGR;
        s_screen_width = ILI9341_WIDTH;
        s_screen_height = ILI9341_HEIGHT;
        s_window_width = window_width;
        s_window_height = window_height;
        break;
    case 2:
        rotation = MADCTL_MY | MADCTL_BGR;
        s_screen_width = ILI9341_HEIGHT;
        s_screen_height = ILI9341_WIDTH;
        s_window_width = window_height;
        s_window_height = window_width;
        break;
    case 3:
        rotation = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR;
        s_screen_width = ILI9341_WIDTH;
        s_screen_height = ILI9341_HEIGHT;
        s_window_width = window_width;
        s_window_height = window_height;
        break;
    }
    ili9341WriteCommand(ILI9341_MADCTL);
    ili9341WriteData(rotation);

    s_window_x_offset = (s_screen_width - s_window_width) / 2U;
    s_window_y_offset = (s_screen_height - s_window_height) / 2U;
}

void ili9341FillScreen(uint16_t color)
{
    ili9341SetAddrWindowScreen(0, 0, s_screen_width, s_screen_height);
    uint32_t count = (uint32_t)s_screen_width * s_screen_height;
    for (uint32_t i = 0U; i < count; ++i)
    {
        ili9341WriteData(color);
    }
}

void ili9341Init(uint8_t rotation, uint16_t window_width, uint16_t window_height)
{
    fsmcInit();
    ili9341Reset();
    delay(120U);

    // SW reset
    ili9341WriteCommand(0x01);
    delay(5U);

    {
        ili9341WriteCommand(ILI9341_PWCTRA);
        const uint8_t data[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_PWCTRB);
        const uint8_t data[] = {0x00, 0xC1, 0x30};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_DRIVERTIMINGCTR_A);
        const uint8_t data[] = {0x85, 0x00, 0x78};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_DRIVERTIMINGCTR_B);
        const uint8_t data[] = {0x00, 0x00};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_PWRON_SEQ_CTR);
        const uint8_t data[] = {0x64, 0x03, 0x12, 0x81};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_PUMP_RATIO);
        const uint8_t data[] = {0x20};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_PWCTR1);
        const uint8_t data[] = {0x23};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_PWCTR2);
        const uint8_t data[] = {0x10};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_VMCTR1);
        const uint8_t data[] = {0x3E, 0x28};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_VMCTR2);
        const uint8_t data[] = {0x86};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_MADCTL);
        const uint8_t data[] = {0x48};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_PIXFMT);
        const uint8_t data[] = {0x55}; // 16-bit RGB565
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_FRMCTR1);
        const uint8_t data[] = {0x00, 0x18};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_DFUNCTR);
        const uint8_t data[] = {0x08, 0x82, 0x27};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_3G);
        const uint8_t data[] = {0x00};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_GAMMASET);
        const uint8_t data[] = {0x01};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_GMCTRP1);
        const uint8_t data[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                                0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
        ili9341WriteDataBuffer(data, sizeof(data));
    }
    {
        ili9341WriteCommand(ILI9341_GMCTRN1);
        const uint8_t data[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                                0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
        ili9341WriteDataBuffer(data, sizeof(data));
    }

    delay(5U);
    ili9341WriteCommand(ILI9341_SLPOUT);
    delay(120U);
    ili9341WriteCommand(ILI9341_DISPON);
    ili9341SetDisplayRotation(rotation, window_width, window_height);

    ili9341FillScreen(ILI9341_BLACK);
    LOGGER_LOG_INFO(LOGGER_DISPLAY, "ILI9341 init: %ux%u, rotation %u", (unsigned)window_width, (unsigned)window_height, (unsigned)rotation);
}

void ili9341DrawScanlines(const uint8_t lines, const uint16_t lines_offset, const uint16_t array_size, const uint16_t *data)
{
    if (lines == 0)
    {
        LOGGER_LOG_ERROR(LOGGER_DISPLAY, "DrawScanlines: lines=0");
        return;
    }
    if (lines_offset >= s_window_height)
    {
        LOGGER_LOG_ERROR(LOGGER_DISPLAY, "DrawScanlines: offset %u >= height %u",
                         (unsigned)lines_offset, (unsigned)s_window_height);
        return;
    }
    if ((lines_offset + lines) > s_window_height)
    {
        LOGGER_LOG_ERROR(LOGGER_DISPLAY, "DrawScanlines: offset %u + lines %u > height %u",
                         (unsigned)lines_offset, (unsigned)lines, (unsigned)s_window_height);
        return;
    }
    if (array_size != (uint32_t)s_window_width * lines)
    {
        LOGGER_LOG_ERROR(LOGGER_DISPLAY, "DrawScanlines: array_size %u != width %u * lines %u",
                         (unsigned)array_size, (unsigned)s_window_width, (unsigned)lines);
        return;
    }

    ili9341SetAddrWindowScreen(s_window_x_offset, s_window_y_offset + lines_offset, s_window_width, lines);
    fsmcDmaSend(data, array_size);
}