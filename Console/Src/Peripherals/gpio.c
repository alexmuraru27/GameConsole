#include "Peripherals/gpio.h"
#include <stm32f407xx.h>
#include "Peripherals/sysclock.h"
#include "Logger/logger.h"

/*
 * Pin configuration is table-driven: each subsystem lists its pins as a
 * GpioPinConfig array and gpioConfigurePin() applies one pin's MODER / OSPEEDR /
 * PUPDR / OTYPER / AFR fields with a read-modify-write that touches only that
 * pin (every other bit — including the untouched SWD/JTAG pins — is preserved).
 * The tables are the pinout a maintainer audits against docu/HW.md; genuinely
 * special steps (driving a reset/enable line high, the analog ADC pins) stay as
 * explicit code next to their table.
 */

/* GPIO_MODE_* (MODER field values) are shared with callers via gpio.h. */

/* PUPDR field values (2 bits/pin). */
#define GPIO_PUPD_NONE 0U
#define GPIO_PUPD_UP   1U
#define GPIO_PUPD_DOWN 2U

/* OSPEEDR field values (2 bits/pin). */
#define GPIO_SPEED_LOW      0U
#define GPIO_SPEED_MEDIUM   1U
#define GPIO_SPEED_HIGH     2U
#define GPIO_SPEED_VERYHIGH 3U

/* OTYPER field values (1 bit/pin). */
#define GPIO_OTYPE_PUSHPULL  0U
#define GPIO_OTYPE_OPENDRAIN 1U

typedef struct
{
    GPIO_TypeDef *port;
    uint8_t pin;    /* 0..15 */
    uint8_t mode;   /* GPIO_MODE_*   */
    uint8_t af;     /* 0..15, applied to AFR (only meaningful when mode == AF) */
    uint8_t pupd;   /* GPIO_PUPD_*   */
    uint8_t ospeed; /* GPIO_SPEED_*  */
    uint8_t otype;  /* GPIO_OTYPE_*  */
} GpioPinConfig;

/* Apply one pin's fields via read-modify-write so only this pin's bits change. */
static void gpioConfigurePin(const GpioPinConfig *const c)
{
    const uint32_t pin = c->pin;
    const uint32_t field2 = pin * 2U;               /* 2-bit fields: MODER/OSPEEDR/PUPDR */
    const uint32_t afr_idx = pin >> 3U;             /* AFR[0] = pins 0-7, AFR[1] = 8-15 */
    const uint32_t afr_shift = (pin & 0x7U) * 4U;

    c->port->MODER = (c->port->MODER & ~(0x3U << field2)) | ((uint32_t)c->mode << field2);
    c->port->OSPEEDR = (c->port->OSPEEDR & ~(0x3U << field2)) | ((uint32_t)c->ospeed << field2);
    c->port->PUPDR = (c->port->PUPDR & ~(0x3U << field2)) | ((uint32_t)c->pupd << field2);
    c->port->OTYPER = (c->port->OTYPER & ~(0x1U << pin)) | ((uint32_t)c->otype << pin);
    c->port->AFR[afr_idx] = (c->port->AFR[afr_idx] & ~(0xFU << afr_shift)) | ((uint32_t)c->af << afr_shift);
}

static void gpioConfigurePins(const GpioPinConfig *const pins, const uint32_t count)
{
    for (uint32_t i = 0U; i < count; i++)
    {
        gpioConfigurePin(&pins[i]);
    }
}

static void initGpioEsp01(void)
{
    // ESP-01 link on USART1 + bootstrap control pins (see docu/HW.md):
    //   PA9  (USART1 TX, AF7)   PA10 (USART1 RX, AF7)
    //   PB10 (EN/CH_PD, GPIO)   PB6  (RST, GPIO)
    //   PC6  (IO0/GPIO0, GPIO)  PC13 (IO2/GPIO2, GPIO)
    // EN and RST are console-owned (driven high to enable/run). IO0 and IO2 are
    // functional GPIOs of the *running* ESP firmware (the ESP-01S LED is on
    // GPIO2), so the console must NOT hold them: they idle as inputs (Hi-Z) and
    // the module's pull-ups keep them high for a normal boot. The flasher only
    // drives IO0 low transiently to enter the ROM bootloader (esp01SetBootloader);
    // IO2 is never driven, leaving GPIO2 free for the firmware.
    static const GpioPinConfig pins[] = {
        // TX has no pull; RX pulls up so the line idles high when the ESP is quiet.
        {GPIOA, 9, GPIO_MODE_AF, 7, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 10, GPIO_MODE_AF, 7, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        // EN + RST: push-pull outputs, driven high below to enable/run the ESP.
        {GPIOB, GPIO_ESP_EN, GPIO_MODE_OUTPUT, 0, GPIO_PUPD_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOB, GPIO_ESP_RST, GPIO_MODE_OUTPUT, 0, GPIO_PUPD_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        // IO0 + IO2: inputs with pull-up so both straps read high for a normal ESP
        // boot without fighting the firmware (a running ESP drives them push-pull).
        {GPIOC, GPIO_ESP_IO0, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOC, GPIO_ESP_IO2, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));

    // Drive EN and RST high: enabled and out of reset.
    gpioSetPin(GPIOB, GPIO_ESP_EN);
    gpioSetPin(GPIOB, GPIO_ESP_RST);
}

static void initGpioJoystick(void)
{
    // Two d-pads + two special buttons, all active-low inputs with pull-ups:
    //   PA0  Right UP     PA1  Right RIGHT   PA5  Right DOWN   PA6  Right LEFT
    //   PA7  Left  UP     PA8  Left  RIGHT   PA11 Left  DOWN   PA12 Left  LEFT
    //   PB11 Special Button 1                PB12 Special Button 2
    static const GpioPinConfig pins[] = {
        {GPIOA, 0, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 1, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 5, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 6, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 7, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 8, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 11, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 12, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOB, 11, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOB, 12, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));
}

static void initGpioAdc1(void)
{
    // PC0-PC3: analog inputs (joystick axes). Analog mode ignores OSPEEDR/OTYPER/AFR.
    static const GpioPinConfig pins[] = {
        {GPIOC, 0, GPIO_MODE_ANALOG, 0, GPIO_PUPD_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 1, GPIO_MODE_ANALOG, 0, GPIO_PUPD_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 2, GPIO_MODE_ANALOG, 0, GPIO_PUPD_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 3, GPIO_MODE_ANALOG, 0, GPIO_PUPD_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));
}

static void initGpioBuzzer(void)
{
    // PB5 -> TIM3_CH2 PWM (AF2). Internal pull-down holds the pin at 0 V during the
    // reset/boot window (before the timer drives it) so it doesn't float high and
    // bias the buzzer.
    static const GpioPinConfig pins[] = {
        {GPIOB, 5, GPIO_MODE_AF, 2, GPIO_PUPD_DOWN, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));
}

static void initSdio(void)
{
    // SDIO 4-bit bus (AF12) + card detect:
    //   PC8-PC11 D0-D3   PC12 CK   PD2 CMD   PD3 card-detect (input)
    // Data/CMD lines pull up; CK does not. Card detect is active-low (0 = present).
    static const GpioPinConfig pins[] = {
        {GPIOD, GPIO_SD_DETECT, GPIO_MODE_INPUT, 0, GPIO_PUPD_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 8, GPIO_MODE_AF, 12, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 9, GPIO_MODE_AF, 12, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 10, GPIO_MODE_AF, 12, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 11, GPIO_MODE_AF, 12, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOC, 12, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL}, /* CK: no pull-up */
        {GPIOD, 2, GPIO_MODE_AF, 12, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));
}

static void initI2C1(void)
{
    // PB8 (I2C1_SCL), PB9 (I2C1_SDA) -> AF4, open-drain with pull-up.
    static const GpioPinConfig pins[] = {
        {GPIOB, GPIO_I2C_SCL, GPIO_MODE_AF, 4, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_OPENDRAIN},
        {GPIOB, GPIO_I2C_SDA, GPIO_MODE_AF, 4, GPIO_PUPD_UP, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_OPENDRAIN},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));
}

static void initGpioFsmc(void)
{
    // FSMC 16-bit 8080 parallel interface for the ILI9341 (all data/control AF12):
    //   Data:    PD14(D0) PD15(D1) PD0(D2) PD1(D3) PE7(D4) PE8(D5) PE9(D6) PE10(D7)
    //            PE11(D8) PE12(D9) PE13(D10) PE14(D11) PE15(D12) PD8(D13) PD9(D14) PD10(D15)
    //   Control: PD4(NOE/RD) PD5(NWE/WR) PD7(NE1/CS) PD11(A16/DC)
    //   RST: PC7 (GPIO output, driven high below)
    //   BL:  PA3 (TIM9_CH2 PWM, AF3 — backlightInit() drives the duty)
    static const GpioPinConfig pins[] = {
        // Port D FSMC lines.
        {GPIOD, 0, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 1, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 4, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 5, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 7, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 8, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 9, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 10, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 11, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 14, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOD, 15, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        // Port E FSMC lines PE7-PE15.
        {GPIOE, 7, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 8, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 9, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 10, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 11, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 12, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 13, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 14, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOE, 15, GPIO_MODE_AF, 12, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        // Display reset (GPIO output, driven high below); PA3 backlight PWM (AF3).
        {GPIOC, GPIO_DISPLAY_RST, GPIO_MODE_OUTPUT, 0, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
        {GPIOA, 3, GPIO_MODE_AF, 3, GPIO_PUPD_NONE, GPIO_SPEED_VERYHIGH, GPIO_OTYPE_PUSHPULL},
    };
    gpioConfigurePins(pins, sizeof(pins) / sizeof(pins[0]));

    // Release the ILI9341 from reset (drive it high). backlightInit() takes over PA3.
    gpioSetPin(GPIOC, GPIO_DISPLAY_RST);
}

void gpioInit(void)
{
    initGpioEsp01();
    initGpioJoystick();
    initGpioAdc1();
    initGpioBuzzer();
    initSdio();
    initI2C1();
    initGpioFsmc();
    LOGGER_LOG_DEBUG(LOGGER_CORE, "gpio init: joystick/ADC/buzzer/SDIO/I2C/FSMC pins");
}

/* ------------------------------------------------------------------ *
 *  Single-pin runtime I/O (see gpio.h). BSRR is write-1-to-act and
 *  atomic (no read-modify-write), so set/clear never race an ISR that
 *  touches another pin on the same port.
 * ------------------------------------------------------------------ */

void gpioSetPin(GPIO_TypeDef *const port, const uint8_t pin)
{
    port->BSRR = (uint32_t)1U << pin; /* BSRR[pin] = set */
}

void gpioClearPin(GPIO_TypeDef *const port, const uint8_t pin)
{
    port->BSRR = (uint32_t)1U << (pin + 16U); /* BSRR[pin+16] = reset */
}

void gpioWritePin(GPIO_TypeDef *const port, const uint8_t pin, const bool level)
{
    if (level)
    {
        gpioSetPin(port, pin);
    }
    else
    {
        gpioClearPin(port, pin);
    }
}

bool gpioReadPin(GPIO_TypeDef *const port, const uint8_t pin)
{
    return (port->IDR & ((uint32_t)1U << pin)) != 0U;
}

void gpioSetPinMode(GPIO_TypeDef *const port, const uint8_t pin, const uint8_t mode)
{
    const uint32_t field2 = (uint32_t)pin * 2U;
    port->MODER = (port->MODER & ~(0x3U << field2)) | ((uint32_t)mode << field2);
}
