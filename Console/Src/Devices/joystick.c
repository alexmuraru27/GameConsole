#include "joystick.h"
#include "adc.h"
#include <stm32f407xx.h>
#include <stdio.h>

#define JOYSTICK_DATA_R_BTN_DUP_POS 0U
#define JOYSTICK_DATA_R_BTN_DUP_MASK (1U << JOYSTICK_DATA_R_BTN_DUP_POS)
#define JOYSTICK_DATA_R_BTN_DRIGHT_POS 1U
#define JOYSTICK_DATA_R_BTN_DRIGHT_MASK (1U << JOYSTICK_DATA_R_BTN_DRIGHT_POS)
#define JOYSTICK_DATA_R_BTN_DDOWN_POS 2U
#define JOYSTICK_DATA_R_BTN_DDOWN_MASK (1U << JOYSTICK_DATA_R_BTN_DDOWN_POS)
#define JOYSTICK_DATA_R_BTN_DLEFT_POS 3U
#define JOYSTICK_DATA_R_BTN_DLEFT_MASK (1U << JOYSTICK_DATA_R_BTN_DLEFT_POS)
#define JOYSTICK_DATA_R_BTN_MASK (JOYSTICK_DATA_R_BTN_DUP_MASK | JOYSTICK_DATA_R_BTN_DRIGHT_MASK | JOYSTICK_DATA_R_BTN_DDOWN_MASK | JOYSTICK_DATA_R_BTN_DLEFT_MASK)

#define JOYSTICK_DATA_L_BTN_DUP_POS 4U
#define JOYSTICK_DATA_L_BTN_DUP_MASK (1U << JOYSTICK_DATA_L_BTN_DUP_POS)
#define JOYSTICK_DATA_L_BTN_DRIGHT_POS 5U
#define JOYSTICK_DATA_L_BTN_DRIGHT_MASK (1U << JOYSTICK_DATA_L_BTN_DRIGHT_POS)
#define JOYSTICK_DATA_L_BTN_DDOWN_POS 6U
#define JOYSTICK_DATA_L_BTN_DDOWN_MASK (1U << JOYSTICK_DATA_L_BTN_DDOWN_POS)
#define JOYSTICK_DATA_L_BTN_DLEFT_POS 7U
#define JOYSTICK_DATA_L_BTN_DLEFT_MASK (1U << JOYSTICK_DATA_L_BTN_DLEFT_POS)
#define JOYSTICK_DATA_L_BTN_MASK (JOYSTICK_DATA_L_BTN_DUP_MASK | JOYSTICK_DATA_L_BTN_DRIGHT_MASK | JOYSTICK_DATA_L_BTN_DDOWN_MASK | JOYSTICK_DATA_L_BTN_DLEFT_MASK)

#define JOYSTICK_DATA_BTN_MASK (JOYSTICK_DATA_R_BTN_MASK | JOYSTICK_DATA_L_BTN_MASK)

#define JOYSTICK_DATA_SPECIAL_BTN_1_POS 8U
#define JOYSTICK_DATA_SPECIAL_BTN_1_MASK (1U << JOYSTICK_DATA_SPECIAL_BTN_1_POS)
#define JOYSTICK_DATA_SPECIAL_BTN_2_POS 9U
#define JOYSTICK_DATA_SPECIAL_BTN_2_MASK (1U << JOYSTICK_DATA_SPECIAL_BTN_2_POS)
#define JOYSTICK_DATA_SPECIAL_BTN_MASK (JOYSTICK_DATA_SPECIAL_BTN_1_MASK | JOYSTICK_DATA_SPECIAL_BTN_2_MASK)

#define JOYSTICK_DATA_R_ANALOG_Y_POS 10U
#define JOYSTICK_DATA_R_ANALOG_Y_MASK (3U << JOYSTICK_DATA_R_ANALOG_Y_POS)
#define JOYSTICK_DATA_R_ANALOG_X_POS 12U
#define JOYSTICK_DATA_R_ANALOG_X_MASK (3U << JOYSTICK_DATA_R_ANALOG_X_POS)
#define JOYSTICK_DATA_L_ANALOG_Y_POS 14U
#define JOYSTICK_DATA_L_ANALOG_Y_MASK (3U << JOYSTICK_DATA_L_ANALOG_Y_POS)
#define JOYSTICK_DATA_L_ANALOG_X_POS 16U
#define JOYSTICK_DATA_L_ANALOG_X_MASK (3U << JOYSTICK_DATA_L_ANALOG_X_POS)
#define JOYSTICK_DATA_ANALOG_MASK (JOYSTICK_DATA_R_ANALOG_Y_MASK | JOYSTICK_DATA_R_ANALOG_X_MASK | JOYSTICK_DATA_L_ANALOG_Y_MASK | JOYSTICK_DATA_L_ANALOG_X_MASK)


#define ANALOG_THRESHOLD 1500U
#define ANALOG_LOWER_THRESHOLD (2048U - ANALOG_THRESHOLD)
#define ANALOG_HIGHER_THRESHOLD (2048U + ANALOG_THRESHOLD)
// 8 buttons dpad - 8 bits
// 2 special buttons - 2 bits
// 4 axes = 8 bits -> 00 off, 01 low axis, 02 max axis
// [0000 0000][0 0 0 0 0 0 2_L_AnalogX] [2_L_AnalogY 2_R_AnalogX 2_R_AnalogY Special1 Special2] [L_DLeft L_DDown L_DRight L_DUp R_DLeft R_DDown R_DRight R_DUp]
static volatile uint32_t s_joystick_data = 0U;
static volatile uint32_t s_joystick_prev_btn = 0U;
static volatile uint16_t *s_buffer_addr = 0U;
static const uint32_t s_axis_mask[] = {
    JOYSTICK_DATA_L_ANALOG_X_MASK,
    JOYSTICK_DATA_L_ANALOG_Y_MASK,
    JOYSTICK_DATA_R_ANALOG_X_MASK,
    JOYSTICK_DATA_R_ANALOG_Y_MASK};

static const uint8_t s_axis_shift[] = {
    JOYSTICK_DATA_L_ANALOG_X_POS,
    JOYSTICK_DATA_L_ANALOG_Y_POS,
    JOYSTICK_DATA_R_ANALOG_X_POS,
    JOYSTICK_DATA_R_ANALOG_Y_POS};

typedef enum RawJoystickAnalogValue
{
    RawJoystickAnalogValueMid = 0U,
    RawJoystickAnalogValueLow = 1U,
    RawJoystickAnalogValueHigh = 2U
} RawJoystickAnalogValue;

void joystickReadData(void)
{
    // Keep this as short as possible!
    // Clear the dpad and special buttons
    s_joystick_data = 0U;

    // Set dpad buttons from GPIOA (active low, pull-up)
    const uint32_t idr_a = ~GPIOA->IDR;
    s_joystick_data |= (((idr_a >> 0U)  & 1U) << JOYSTICK_DATA_R_BTN_DUP_POS)    |  // PA0  -> R_Up
                       (((idr_a >> 1U)  & 1U) << JOYSTICK_DATA_R_BTN_DRIGHT_POS)  |  // PA1  -> R_Right
                       (((idr_a >> 5U)  & 1U) << JOYSTICK_DATA_R_BTN_DDOWN_POS)   |  // PA5  -> R_Down
                       (((idr_a >> 6U)  & 1U) << JOYSTICK_DATA_R_BTN_DLEFT_POS)   |  // PA6  -> R_Left
                       (((idr_a >> 7U)  & 1U) << JOYSTICK_DATA_L_BTN_DUP_POS)     |  // PA7  -> L_Up
                       (((idr_a >> 8U)  & 1U) << JOYSTICK_DATA_L_BTN_DRIGHT_POS)  |  // PA8  -> L_Right
                       (((idr_a >> 11U) & 1U) << JOYSTICK_DATA_L_BTN_DDOWN_POS)   |  // PA11 -> L_Down
                       (((idr_a >> 12U) & 1U) << JOYSTICK_DATA_L_BTN_DLEFT_POS);     // PA12 -> L_Left

    // Special buttons: PB11 -> Sp1 (launch), PB12 -> Sp2 (exit), active-low
    const uint32_t idr_b = GPIOB->IDR;
    if (!(idr_b & (1U << 11U))) { s_joystick_data |= JOYSTICK_DATA_SPECIAL_BTN_1_MASK; }
    if (!(idr_b & (1U << 12U))) { s_joystick_data |= JOYSTICK_DATA_SPECIAL_BTN_2_MASK; }

    // Analog values
    if (s_buffer_addr)
    {
        uint16_t val = 0U;
        for (uint8_t i = 0U; i < 4U; ++i)
        {
            val = s_buffer_addr[i];
            if (val < ANALOG_LOWER_THRESHOLD || val > ANALOG_HIGHER_THRESHOLD)
            {
                s_joystick_data |= (((val < ANALOG_LOWER_THRESHOLD) ? RawJoystickAnalogValueLow : RawJoystickAnalogValueHigh) << s_axis_shift[i]) & s_axis_mask[i];
            }
        }
    }

    const uint32_t all_btn_mask = JOYSTICK_DATA_BTN_MASK | JOYSTICK_DATA_SPECIAL_BTN_MASK;
    const uint32_t new_btn = s_joystick_data & all_btn_mask;
    const uint32_t changed = new_btn ^ s_joystick_prev_btn;
    if (changed)
    {
        static const char *const btn_names[] = {
            "R_Up", "R_Right", "R_Down", "R_Left",
            "L_Up", "L_Right", "L_Down", "L_Left",
            "Special1", "Special2"
        };
        for (uint8_t i = 0U; i < 10U; ++i)
        {
            if (changed & (1U << i))
            {
                printf("BTN %s %s\n", btn_names[i], (new_btn & (1U << i)) ? "PRESS" : "RELEASE");
            }
        }
        s_joystick_prev_btn = new_btn;
    }
}

void joystickInit(void)
{
    s_joystick_data = 0U;
    s_buffer_addr = getAdc1BufferAddress();
}

bool joystickGetRBtnUp(void)
{
    return (s_joystick_data & JOYSTICK_DATA_R_BTN_DUP_MASK) != 0U;
}
bool joystickGetRBtnRight(void)
{
    return (s_joystick_data & JOYSTICK_DATA_R_BTN_DRIGHT_MASK) != 0U;
}
bool joystickGetRBtnDown(void)
{
    return (s_joystick_data & JOYSTICK_DATA_R_BTN_DDOWN_MASK) != 0U;
}
bool joystickGetRBtnLeft(void)
{
    return (s_joystick_data & JOYSTICK_DATA_R_BTN_DLEFT_MASK) != 0U;
}
bool joystickGetLBtnUp(void)
{
    return (s_joystick_data & JOYSTICK_DATA_L_BTN_DUP_MASK) != 0U;
}
bool joystickGetLBtnRight(void)
{
    return (s_joystick_data & JOYSTICK_DATA_L_BTN_DRIGHT_MASK) != 0U;
}
bool joystickGetLBtnDown(void)
{
    return (s_joystick_data & JOYSTICK_DATA_L_BTN_DDOWN_MASK) != 0U;
}
bool joystickGetLBtnLeft(void)
{
    return (s_joystick_data & JOYSTICK_DATA_L_BTN_DLEFT_MASK) != 0U;
}
bool joystickGetSpecialBtn1(void)
{
    return (s_joystick_data & JOYSTICK_DATA_SPECIAL_BTN_1_MASK) != 0U;
}
bool joystickGetSpecialBtn2(void)
{
    return (s_joystick_data & JOYSTICK_DATA_SPECIAL_BTN_2_MASK) != 0U;
}
JoystickAnalogValue joystickGetRAnalogY(void)
{
    JoystickAnalogValue joyRAnalogY = JoystickAnalogValueOff;
    switch ((s_joystick_data & JOYSTICK_DATA_R_ANALOG_Y_MASK) >> JOYSTICK_DATA_R_ANALOG_Y_POS)
    {
    case RawJoystickAnalogValueLow:
    {
        joyRAnalogY = JoystickAnalogValueLowAxis;
        break;
    }
    case RawJoystickAnalogValueHigh:
    {
        joyRAnalogY = JoystickAnalogValueHighAxis;
        break;
    }
    default:
        break;
    }
    return joyRAnalogY;
}
JoystickAnalogValue joystickGetRAnalogX(void)
{
    JoystickAnalogValue joyRAnalogX = JoystickAnalogValueOff;
    switch ((s_joystick_data & JOYSTICK_DATA_R_ANALOG_X_MASK) >> JOYSTICK_DATA_R_ANALOG_X_POS)
    {
    case RawJoystickAnalogValueLow:
    {
        joyRAnalogX = JoystickAnalogValueLowAxis;
        break;
    }
    case RawJoystickAnalogValueHigh:
    {
        joyRAnalogX = JoystickAnalogValueHighAxis;
        break;
    }
    default:
        break;
    }
    return joyRAnalogX;
}
JoystickAnalogValue joystickGetLAnalogY(void)
{
    // Joystick is flipped so we need to also flip the axes
    JoystickAnalogValue joyLAnalogY = JoystickAnalogValueOff;
    switch ((s_joystick_data & JOYSTICK_DATA_L_ANALOG_Y_MASK) >> JOYSTICK_DATA_L_ANALOG_Y_POS)
    {
    case RawJoystickAnalogValueLow:
    {
        joyLAnalogY = JoystickAnalogValueHighAxis;
        break;
    }
    case RawJoystickAnalogValueHigh:
    {
        joyLAnalogY = JoystickAnalogValueLowAxis;
        break;
    }
    default:
        break;
    }
    return joyLAnalogY;
}
JoystickAnalogValue joystickGetLAnalogX(void)
{
    // Joystick is flipped so we need to also flip the axes
    JoystickAnalogValue joyLAnalogX = JoystickAnalogValueOff;
    switch ((s_joystick_data & JOYSTICK_DATA_L_ANALOG_X_MASK) >> JOYSTICK_DATA_L_ANALOG_X_POS)
    {
    case RawJoystickAnalogValueLow:
    {
        joyLAnalogX = JoystickAnalogValueHighAxis;
        break;
    }
    case RawJoystickAnalogValueHigh:
    {
        joyLAnalogX = JoystickAnalogValueLowAxis;
        break;
    }
    default:
        break;
    }
    return joyLAnalogX;
}

bool joystickIsAnyButtonPressed(void)
{
    return ((s_joystick_data & JOYSTICK_DATA_BTN_MASK) != 0U);
}