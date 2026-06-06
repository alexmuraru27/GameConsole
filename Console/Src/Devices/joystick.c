#include "joystick.h"
#include "adc.h"
#include "gpio.h"
#include "sysclock.h"
#include <stdio.h>
// Button bits (within s_btn_data)
typedef enum JoystickBtnBit
{
    JoystickBtnBitRUp = 0U,
    JoystickBtnBitRRight = 1U,
    JoystickBtnBitRDown = 2U,
    JoystickBtnBitRLeft = 3U,
    JoystickBtnBitLUp = 4U,
    JoystickBtnBitLRight = 5U,
    JoystickBtnBitLDown = 6U,
    JoystickBtnBitLLeft = 7U,
    JoystickBtnBitSpecial1 = 8U,
    JoystickBtnBitSpecial2 = 9U,
} JoystickBtnBit;

#define BTN_COUNT 10U

#define BTN_MASK_R_UP (1U << JoystickBtnBitRUp)
#define BTN_MASK_R_RIGHT (1U << JoystickBtnBitRRight)
#define BTN_MASK_R_DOWN (1U << JoystickBtnBitRDown)
#define BTN_MASK_R_LEFT (1U << JoystickBtnBitRLeft)
#define BTN_MASK_L_UP (1U << JoystickBtnBitLUp)
#define BTN_MASK_L_RIGHT (1U << JoystickBtnBitLRight)
#define BTN_MASK_L_DOWN (1U << JoystickBtnBitLDown)
#define BTN_MASK_L_LEFT (1U << JoystickBtnBitLLeft)
#define BTN_MASK_SPECIAL1 (1U << JoystickBtnBitSpecial1)
#define BTN_MASK_SPECIAL2 (1U << JoystickBtnBitSpecial2)
#define BTN_MASK_DPAD_R (BTN_MASK_R_UP | BTN_MASK_R_RIGHT | BTN_MASK_R_DOWN | BTN_MASK_R_LEFT)
#define BTN_MASK_DPAD_L (BTN_MASK_L_UP | BTN_MASK_L_RIGHT | BTN_MASK_L_DOWN | BTN_MASK_L_LEFT)
#define BTN_MASK_ALL (BTN_MASK_DPAD_R | BTN_MASK_DPAD_L | BTN_MASK_SPECIAL1 | BTN_MASK_SPECIAL2)

// Analog bits (within s_analog_data, 2 bits per axis)
typedef enum JoystickAnalogBit
{
    JoystickAnalogBitRY = 0U,
    JoystickAnalogBitRX = 2U,
    JoystickAnalogBitLY = 4U,
    JoystickAnalogBitLX = 6U,
} JoystickAnalogBit;

#define ANALOG_COUNT 4U

#define ANALOG_MASK_RY (3U << JoystickAnalogBitRY)
#define ANALOG_MASK_RX (3U << JoystickAnalogBitRX)
#define ANALOG_MASK_LY (3U << JoystickAnalogBitLY)
#define ANALOG_MASK_LX (3U << JoystickAnalogBitLX)

#define ANALOG_THRESHOLD 1500U
#define ANALOG_LOWER_THRESHOLD (2048U - ANALOG_THRESHOLD)
#define ANALOG_HIGHER_THRESHOLD (2048U + ANALOG_THRESHOLD)
#define DEBOUNCE_MS 5U

typedef enum RawAnalogState
{
    RawAnalogStateMid = 0U,
    RawAnalogStateLow = 1U,
    RawAnalogStateHigh = 2U,
} RawAnalogState;

static volatile uint32_t s_btn_data = 0U;
static uint8_t s_btn_raw[BTN_COUNT];
static uint32_t s_btn_debounce_timer[BTN_COUNT];

static volatile uint32_t s_analog_data = 0U;
static uint8_t s_analog_raw[ANALOG_COUNT];
static uint32_t s_analog_debounce_timer[ANALOG_COUNT];

static volatile uint16_t *s_buffer_addr = 0U;

static const uint32_t s_axis_mask[] = {
    ANALOG_MASK_LX, ANALOG_MASK_LY,
    ANALOG_MASK_RX, ANALOG_MASK_RY};

static const uint8_t s_axis_shift[] = {
    JoystickAnalogBitLX, JoystickAnalogBitLY,
    JoystickAnalogBitRX, JoystickAnalogBitRY};

static void readButtons(void)
{
    const uint32_t raw_all = gpioReadButtons();
    for (uint8_t i = 0U; i < BTN_COUNT; ++i)
    {
        const uint8_t raw_bit = (raw_all >> i) & 1U;
        if (raw_bit != s_btn_raw[i])
        {
            s_btn_raw[i]            = raw_bit;
            s_btn_debounce_timer[i] = getSysTime();
        }
        else if (getSysTime() - s_btn_debounce_timer[i] >= DEBOUNCE_MS)
        {
            if (raw_bit)
                s_btn_data |=  (1U << i);
            else
                s_btn_data &= ~(1U << i);
        }
    }
}

static void readAnalog(void)
{
    if (!s_buffer_addr)
    {
        printf("joystick: analog buffer address is null\n");
        return;
    }

    for (uint8_t i = 0U; i < ANALOG_COUNT; ++i)
    {
        uint8_t raw_axis = RawAnalogStateMid;
        const uint16_t val = s_buffer_addr[i];
        if (val < ANALOG_LOWER_THRESHOLD)
            raw_axis = RawAnalogStateHigh;
        else if (val > ANALOG_HIGHER_THRESHOLD)
            raw_axis = RawAnalogStateLow;

        if (raw_axis != s_analog_raw[i])
        {
            s_analog_raw[i]            = raw_axis;
            s_analog_debounce_timer[i] = getSysTime();
        }
        else if (getSysTime() - s_analog_debounce_timer[i] >= DEBOUNCE_MS)
        {
            s_analog_data = (s_analog_data & ~s_axis_mask[i]) |
                            ((uint32_t)(raw_axis << s_axis_shift[i]) & s_axis_mask[i]);
        }
    }
}

void joystickReadData(void)
{
    readButtons();
    readAnalog();
}

void joystickInit(void)
{
    s_btn_data = 0U;
    s_analog_data = 0U;
    s_buffer_addr = getAdc1BufferAddress();

    const uint32_t now = getSysTime();
    for (uint8_t i = 0U; i < BTN_COUNT; ++i)
    {
        s_btn_raw[i] = 0U;
        s_btn_debounce_timer[i] = now;
    }
    for (uint8_t i = 0U; i < ANALOG_COUNT; ++i)
    {
        s_analog_raw[i] = RawAnalogStateMid;
        s_analog_debounce_timer[i] = now;
    }
}

bool joystickGetRBtnUp(void)
{
    return (s_btn_data & BTN_MASK_R_UP) != 0U;
}
bool joystickGetRBtnRight(void)
{
    return (s_btn_data & BTN_MASK_R_RIGHT) != 0U;
}
bool joystickGetRBtnDown(void)
{
    return (s_btn_data & BTN_MASK_R_DOWN) != 0U;
}
bool joystickGetRBtnLeft(void)
{
    return (s_btn_data & BTN_MASK_R_LEFT) != 0U;
}
bool joystickGetLBtnUp(void)
{
    return (s_btn_data & BTN_MASK_L_UP) != 0U;
}
bool joystickGetLBtnRight(void)
{
    return (s_btn_data & BTN_MASK_L_RIGHT) != 0U;
}
bool joystickGetLBtnDown(void)
{
    return (s_btn_data & BTN_MASK_L_DOWN) != 0U;
}
bool joystickGetLBtnLeft(void)
{
    return (s_btn_data & BTN_MASK_L_LEFT) != 0U;
}
bool joystickGetSpecialBtn1(void)
{
    return (s_btn_data & BTN_MASK_SPECIAL1) != 0U;
}
bool joystickGetSpecialBtn2(void)
{
    return (s_btn_data & BTN_MASK_SPECIAL2) != 0U;
}
bool joystickIsAnyButtonPressed(void)
{
    return (s_btn_data & BTN_MASK_ALL) != 0U;
}

JoystickAxisState joystickGetRAnalogY(void)
{
    switch ((s_analog_data & ANALOG_MASK_RY) >> JoystickAnalogBitRY)
    {
    case RawAnalogStateLow:
        return JoystickAxisStatePositive;
    case RawAnalogStateHigh:
        return JoystickAxisStateNegative;
    default:
        return JoystickAxisStateOff;
    }
}

JoystickAxisState joystickGetRAnalogX(void)
{
    switch ((s_analog_data & ANALOG_MASK_RX) >> JoystickAnalogBitRX)
    {
    case RawAnalogStateLow:
        return JoystickAxisStateNegative;
    case RawAnalogStateHigh:
        return JoystickAxisStatePositive;
    default:
        return JoystickAxisStateOff;
    }
}

JoystickAxisState joystickGetLAnalogY(void)
{
    switch ((s_analog_data & ANALOG_MASK_LY) >> JoystickAnalogBitLY)
    {
    case RawAnalogStateLow:
        return JoystickAxisStatePositive;
    case RawAnalogStateHigh:
        return JoystickAxisStateNegative;
    default:
        return JoystickAxisStateOff;
    }
}

JoystickAxisState joystickGetLAnalogX(void)
{
    switch ((s_analog_data & ANALOG_MASK_LX) >> JoystickAnalogBitLX)
    {
    case RawAnalogStateLow:
        return JoystickAxisStateNegative;
    case RawAnalogStateHigh:
        return JoystickAxisStatePositive;
    default:
        return JoystickAxisStateOff;
    }
}
