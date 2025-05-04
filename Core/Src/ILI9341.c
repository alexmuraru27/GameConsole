#include "ILI9341.h"
#include "spi.h"
#include "stm32f407xx.h"
#include "sysclock.h"
#include "gpio.h"

#define ILI9341_TFTWIDTH 240  ///< ILI9341 max TFT width
#define ILI9341_TFTHEIGHT 320 ///< ILI9341 max TFT height
static uint16_t g_width;      ///< Display width as modified by current rotation
static uint16_t g_height;     ///< Display height as modified by current rotation
static uint8_t g_rotation;    ///< Display rotation (0 thru 3)

#define ILI9341_SLPOUT 0x11            ///< Sleep Out
#define ILI9341_GAMMASET 0x26          ///< Gamma Set
#define ILI9341_DISPON 0x29            ///< Display ON
#define ILI9341_CASET 0x2A             ///< Column Address Set
#define ILI9341_PASET 0x2B             ///< Page Address Set
#define ILI9341_RAMWR 0x2C             ///< Memory Write
#define ILI9341_MADCTL 0x36            ///< Memory Access Control
#define ILI9341_VSCRSADD 0x37          ///< Vertical Scrolling Start Address
#define ILI9341_PIXFMT 0x3A            ///< COLMOD: Pixel Format Set
#define ILI9341_FRMCTR1 0xB1           ///< Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_DFUNCTR 0xB6           ///< Display Function Control
#define ILI9341_PWCTR1 0xC0            ///< Power Control 1
#define ILI9341_PWCTR2 0xC1            ///< Power Control 2
#define ILI9341_PWCTRA 0xCB            ///< Power Control A
#define ILI9341_PWCTRB 0xCF            ///< Power Control B
#define ILI9341_VMCTR1 0xC5            ///< VCOM Control 1
#define ILI9341_VMCTR2 0xC7            ///< VCOM Control 2
#define ILI9341_GMCTRP1 0xE0           ///< Positive Gamma Correction
#define ILI9341_GMCTRN1 0xE1           ///< Negative Gamma Correction
#define ILI9341_3G 0xF2                ///< Negative Gamma Correction
#define ILI9341_DRIVERTIMINGCTR_B 0xEA ///< Driver Timing control B
#define ILI9341_DRIVERTIMINGCTR_A 0xE8 ///< Driver Timing control A
#define ILI9341_PUMP_RATIO 0xF7
#define ILI9341_PWRON_SEQ_CTR 0xED
#define MADCTL_MY 0x80  ///< Bottom to top
#define MADCTL_MX 0x40  ///< Right to left
#define MADCTL_MV 0x20  ///< Reverse Mode
#define MADCTL_RGB 0x00 ///< Red-Green-Blue pixel order
#define MADCTL_BGR 0x08 ///< Blue-Green-Red pixel order

#ifndef read_byte
#define read_byte(addr) (*(const uint8_t *)(addr))
#endif

static const uint8_t s_init_cmd[] = {
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xEA, 2, 0x00, 0x00,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xF7, 1, 0x20,
    ILI9341_PWCTR1, 1, 0x23,       // Power control VRH[5:0]
    ILI9341_PWCTR2, 1, 0x10,       // Power control SAP[2:0];BT[3:0]
    ILI9341_VMCTR1, 2, 0x3e, 0x28, // VCM control
    ILI9341_VMCTR2, 1, 0x86,       // VCM control2
    ILI9341_MADCTL, 1, 0x48,       // Memory Access Control
    ILI9341_PIXFMT, 1, 0x55,
    ILI9341_FRMCTR1, 2, 0x00, 0x18,
    ILI9341_DFUNCTR, 3, 0x08, 0x82, 0x27,                    // Display Function Control
    0xF2, 1, 0x00,                                           // 3Gamma Function Disable
    ILI9341_GAMMASET, 1, 0x01,                               // Gamma curve selected
    ILI9341_GMCTRP1, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, // Set Gamma
    0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    ILI9341_GMCTRN1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, // Set Gamma
    0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    ILI9341_SLPOUT, 0x80, // Exit Sleep
    ILI9341_DISPON, 0x80, // Display on
    0x00                  // End of list
};

static void toggleDisplayHWReset()
{

    gpioSpi1RstLow();
    delay(400);
    gpioSpi1RstHigh();
}

static void writeCommand(const uint8_t command)
{
    gpioSpi1DcLow();
    spiWrite(command);
}

static void writeData(const uint8_t data)
{
    gpioSpi1DcHigh();
    spiWrite(data);
}

static void sendCommandWithData(const uint8_t commandByte, const uint8_t *dataBytes,
                                const uint8_t numDataBytes)
{
    writeCommand(commandByte);
    for (int i = 0; i < numDataBytes; i++)
    {
        writeData(read_byte(dataBytes++));
    }
}

static void setAddrWindow(const uint16_t x1, const uint16_t y1, const uint16_t w,
                          const uint16_t h)
{
    const uint16_t x2 = (x1 + w - 1);
    const uint16_t y2 = (y1 + h - 1);

    // set column
    writeCommand(ILI9341_CASET);
    writeData(x1 >> 8U);
    writeData(x1);
    writeData(x2 >> 8U);
    writeData(x2);

    // set row
    writeCommand(ILI9341_PASET);
    writeData(y1 >> 8U);
    writeData(y1);
    writeData(y2 >> 8U);
    writeData(y2);

    // set new window to spi slave ram
    writeCommand(ILI9341_RAMWR);
}

void ILI9341Init(void)
{
    SPIInit();
    toggleDisplayHWReset();

    uint8_t cmd, x, numArgs;
    const uint8_t *addr = s_init_cmd;
    while ((cmd = read_byte(addr++)) > 0)
    {
        x = read_byte(addr++);
        numArgs = x & 0x7F;
        sendCommandWithData(cmd, addr, numArgs);

        addr += numArgs;
        if (x & 0x80)
            delay(150);
    }

    g_width = ILI9341_TFTWIDTH;
    g_height = ILI9341_TFTHEIGHT;

    setDisplayRotation(0U);
}

void setDisplayRotation(uint8_t rotation)
{
    g_rotation = rotation % 4; // can't be higher than 3
    switch (g_rotation)
    {
    case 0:
        g_rotation = (MADCTL_MX | MADCTL_BGR);
        g_width = ILI9341_TFTWIDTH;
        g_height = ILI9341_TFTHEIGHT;
        break;
    case 1:
        g_rotation = (MADCTL_MV | MADCTL_BGR);
        g_width = ILI9341_TFTHEIGHT;
        g_height = ILI9341_TFTWIDTH;
        break;
    case 2:
        g_rotation = (MADCTL_MY | MADCTL_BGR);
        g_width = ILI9341_TFTWIDTH;
        g_height = ILI9341_TFTHEIGHT;
        break;
    case 3:
        g_rotation = (MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
        g_width = ILI9341_TFTHEIGHT;
        g_height = ILI9341_TFTWIDTH;
        break;
    }

    sendCommandWithData(ILI9341_MADCTL, &g_rotation, 1);
}

void drawPixel(const uint16_t x, const uint16_t y, const uint16_t colour)
{
    if ((x >= g_width) || (y >= g_height))
        return;

    // first we set the address window
    setAddrWindow(x, y, 1, 1);

    // then we send the color data
    writeData(colour >> 8);
    writeData(colour);
}

void fillScreen(uint16_t colour)
{
    setAddrWindow(50, 50, 100, 100);
    for (uint32_t i = 0; i < 100 * 100 - 50 * 50; i++)
    {
        writeData(colour >> 8);
        writeData(colour);
    }
}
