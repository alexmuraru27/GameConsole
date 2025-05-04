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
volatile uint32_t g_joystick_data = 0U;
volatile uint16_t *g_buffer_addr = 0U;

void joystickReadData(void)
{
    // TODO Call this from timer
    // Clear the dpad and special buttons
    g_joystick_data &= ~(JOYSTICK_DATA_BTN_MASK | JOYSTICK_DATA_SPECIAL_BTN_MASK | JOYSTICK_DATA_ANALOG_MASK);

    // Set dpad buttons
    // Optimised like this due to input pins being in consecutive order starting from
    // PE7 to PE14
    g_joystick_data |= (~((GPIOE->IDR >> JOYSTICK_DATA_BTN_IDR_POS) << JOYSTICK_DATA_BTN_POS) & JOYSTICK_DATA_BTN_MASK);

    // Set special buttons
    // Optimised like this due to input pins being in consecutive order starting from
    // PB11 to PB12
    g_joystick_data |= (~((GPIOB->IDR >> JOYSTICK_DATA_SPECIAL_BTN_IDR_POS) << JOYSTICK_DATA_SPECIAL_BTN_POS) & JOYSTICK_DATA_SPECIAL_BTN_MASK);

    // Analog values
    if (g_buffer_addr)
    {
        if (g_buffer_addr[0U] < ANALOG_LOWER_THRESHOLD || g_buffer_addr[0U] > ANALOG_HIGHER_THRESHOLD)
        {
            g_joystick_data |= (((g_buffer_addr[0U] < ANALOG_LOWER_THRESHOLD) ? ((uint32_t)JoystickAnalogValueLowAxis) : ((uint32_t)JoystickAnalogValueHighAxis)) << JOYSTICK_DATA_L_ANALOG_X_POS) & JOYSTICK_DATA_L_ANALOG_X_MASK;
        }

        if (g_buffer_addr[1U] < ANALOG_LOWER_THRESHOLD || g_buffer_addr[1U] > ANALOG_HIGHER_THRESHOLD)
        {
            g_joystick_data |= (((g_buffer_addr[1U] < ANALOG_LOWER_THRESHOLD) ? ((uint32_t)JoystickAnalogValueLowAxis) : ((uint32_t)JoystickAnalogValueHighAxis)) << JOYSTICK_DATA_L_ANALOG_Y_POS) & JOYSTICK_DATA_L_ANALOG_Y_MASK;
        }

        if (g_buffer_addr[2U] < ANALOG_LOWER_THRESHOLD || g_buffer_addr[2U] > ANALOG_HIGHER_THRESHOLD)
        {
            g_joystick_data |= (((g_buffer_addr[2U] < ANALOG_LOWER_THRESHOLD) ? ((uint32_t)JoystickAnalogValueLowAxis) : ((uint32_t)JoystickAnalogValueHighAxis)) << JOYSTICK_DATA_R_ANALOG_X_POS) & JOYSTICK_DATA_R_ANALOG_X_MASK;
        }

        if (g_buffer_addr[3U] < ANALOG_LOWER_THRESHOLD || g_buffer_addr[3U] > ANALOG_HIGHER_THRESHOLD)
        {
            g_joystick_data |= (((g_buffer_addr[3U] < ANALOG_LOWER_THRESHOLD) ? ((uint32_t)JoystickAnalogValueLowAxis) : ((uint32_t)JoystickAnalogValueHighAxis)) << JOYSTICK_DATA_R_ANALOG_Y_POS) & JOYSTICK_DATA_R_ANALOG_Y_MASK;
        }
    }
}

void joystickInit(void)
{
    g_joystick_data = 0U;
    g_buffer_addr = getAdc1BufferAddress();
}

bool joystickGetRBtnUp(void)
{
    return (g_joystick_data & JOYSTICK_DATA_R_BTN_DUP_MASK) != 0U;
}
bool joystickGetRBtnRight(void)
{
    return (g_joystick_data & JOYSTICK_DATA_R_BTN_DRIGHT_MASK) != 0U;
}
bool joystickGetRBtnDown(void)
{
    return (g_joystick_data & JOYSTICK_DATA_R_BTN_DDOWN_MASK) != 0U;
}
bool joystickGetRBtnLeft(void)
{
    return (g_joystick_data & JOYSTICK_DATA_R_BTN_DLEFT_MASK) != 0U;
}
bool joystickGetLBtnUp(void)
{
    return (g_joystick_data & JOYSTICK_DATA_L_BTN_DUP_MASK) != 0U;
}
bool joystickGetLBtnRight(void)
{
    return (g_joystick_data & JOYSTICK_DATA_L_BTN_DRIGHT_MASK) != 0U;
}
bool joystickGetLBtnDown(void)
{
    return (g_joystick_data & JOYSTICK_DATA_L_BTN_DDOWN_MASK) != 0U;
}
bool joystickGetLBtnLeft(void)
{
    return (g_joystick_data & JOYSTICK_DATA_L_BTN_DLEFT_MASK) != 0U;
}
bool joystickGetSpecialBtn1(void)
{
    return (g_joystick_data & JOYSTICK_DATA_SPECIAL_BTN_1_MASK) != 0U;
}
bool joystickGetSpecialBtn2(void)
{
    return (g_joystick_data & JOYSTICK_DATA_SPECIAL_BTN_2_MASK) != 0U;
}
JoystickAnalogValue joystickGetRAnalogY(void)
{
    return (JoystickAnalogValue)((g_joystick_data & JOYSTICK_DATA_R_ANALOG_Y_MASK) >> JOYSTICK_DATA_R_ANALOG_Y_POS);
}
JoystickAnalogValue joystickGetRAnalogX(void)
{
    return (JoystickAnalogValue)((g_joystick_data & JOYSTICK_DATA_R_ANALOG_X_MASK) >> JOYSTICK_DATA_R_ANALOG_X_POS);
}
JoystickAnalogValue joystickGetLAnalogY(void)
{
    return (JoystickAnalogValue)((g_joystick_data & JOYSTICK_DATA_L_ANALOG_Y_MASK) >> JOYSTICK_DATA_L_ANALOG_Y_POS);
}
JoystickAnalogValue joystickGetLAnalogX(void)
{
    return (JoystickAnalogValue)((g_joystick_data & JOYSTICK_DATA_L_ANALOG_X_MASK) >> JOYSTICK_DATA_L_ANALOG_X_POS);
}
