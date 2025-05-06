#include "joystick.h"
#include "adc.h"
#include <stm32f407xx.h>

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

#define JOYSTICK_DATA_BTN_POS 0U
#define JOYSTICK_DATA_BTN_IDR_POS 7U

#define JOYSTICK_DATA_SPECIAL_BTN_POS 8U
#define JOYSTICK_DATA_SPECIAL_BTN_IDR_POS 11U

#define ANALOG_THRESHOLD 1500U
#define ANALOG_LOWER_THRESHOLD (2048U - ANALOG_THRESHOLD)
#define ANALOG_HIGHER_THRESHOLD (2048U + ANALOG_THRESHOLD)
// 8 buttons dpad - 8 bits
// 2 special buttons - 2 bits
// 4 axes = 8 bits -> 00 off, 01 low axis, 02 max axis
// [0000 0000][0 0 0 0 0 0 2_L_AnalogX] [2_L_AnalogY 2_R_AnalogX 2_R_AnalogY Special1 Special2] [L_DLeft L_DDown L_DRight L_DUp R_DLeft R_DDown R_DRight R_DUp]
static volatile uint32_t s_joystick_data = 0U;
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

void joystickReadData(void)
{
    // Keep this as short as possible!
    // Clear the dpad and special buttons
    s_joystick_data &= ~(JOYSTICK_DATA_BTN_MASK | JOYSTICK_DATA_SPECIAL_BTN_MASK | JOYSTICK_DATA_ANALOG_MASK);

    // Set dpad buttons
    // Optimised like this due to input pins being in consecutive order starting from
    // PE7 to PE14
    s_joystick_data |= (~((GPIOE->IDR >> JOYSTICK_DATA_BTN_IDR_POS) << JOYSTICK_DATA_BTN_POS) & JOYSTICK_DATA_BTN_MASK);

    // Set special buttons
    // Optimised like this due to input pins being in consecutive order starting from
    // PB11 to PB12
    s_joystick_data |= (~((GPIOB->IDR >> JOYSTICK_DATA_SPECIAL_BTN_IDR_POS) << JOYSTICK_DATA_SPECIAL_BTN_POS) & JOYSTICK_DATA_SPECIAL_BTN_MASK);

    // Analog values
    if (s_buffer_addr)
    {
        uint8_t val = 0U;
        for (uint8_t i = 0U; i < 4U; ++i)
        {
            val = s_buffer_addr[i];
            if (val < ANALOG_LOWER_THRESHOLD || val > ANALOG_HIGHER_THRESHOLD)
            {
                s_joystick_data |= ((val < ANALOG_LOWER_THRESHOLD) ? JoystickAnalogValueLowAxis : JoystickAnalogValueHighAxis << s_axis_shift[i]) & s_axis_mask[i];
            }
        }
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
    return (JoystickAnalogValue)((s_joystick_data & JOYSTICK_DATA_R_ANALOG_Y_MASK) >> JOYSTICK_DATA_R_ANALOG_Y_POS);
}
JoystickAnalogValue joystickGetRAnalogX(void)
{
    return (JoystickAnalogValue)((s_joystick_data & JOYSTICK_DATA_R_ANALOG_X_MASK) >> JOYSTICK_DATA_R_ANALOG_X_POS);
}
JoystickAnalogValue joystickGetLAnalogY(void)
{
    return (JoystickAnalogValue)((s_joystick_data & JOYSTICK_DATA_L_ANALOG_Y_MASK) >> JOYSTICK_DATA_L_ANALOG_Y_POS);
}
JoystickAnalogValue joystickGetLAnalogX(void)
{
    return (JoystickAnalogValue)((s_joystick_data & JOYSTICK_DATA_L_ANALOG_X_MASK) >> JOYSTICK_DATA_L_ANALOG_X_POS);
}
