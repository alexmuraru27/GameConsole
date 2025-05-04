#include "joystick.h"
#include "adc.h"
#include <stm32f407xx.h>

#define JOYSTICK_DATA_BTN_MASK 0x00FFU

#define JOYSTICK_DATA_R_BTN_MASK 0x000FU
#define JOYSTICK_DATA_R_BTN_DUP_MASK 0x0001U
#define JOYSTICK_DATA_R_BTN_DRIGHT_MASK 0x0002U
#define JOYSTICK_DATA_R_BTN_DDOWN_MASK 0x0004U
#define JOYSTICK_DATA_R_BTN_DLEFT_MASK 0x0008U

#define JOYSTICK_DATA_L_BTN_MASK 0x00F0U
#define JOYSTICK_DATA_L_BTN_DUP_MASK 0x0010U
#define JOYSTICK_DATA_L_BTN_DRIGHT_MASK 0x0020U
#define JOYSTICK_DATA_L_BTN_DDOWN_MASK 0x0040U
#define JOYSTICK_DATA_L_BTN_DLEFT_MASK 0x0080U

#define JOYSTICK_DATA_SPECIAL_BTN_MASK 0x0300U
#define JOYSTICK_DATA_SPECIAL_BTN_1_MASK 0x0100U
#define JOYSTICK_DATA_SPECIAL_BTN_2_MASK 0x0200U

#define JOYSTICK_DATA_ANALOG_MASK 0x3C00U
#define JOYSTICK_DATA_R_ANALOG_Y_MASK 0x0400U
#define JOYSTICK_DATA_R_ANALOG_X_MASK 0x0800U
#define JOYSTICK_DATA_L_ANALOG_Y_MASK 0x1000U
#define JOYSTICK_DATA_L_ANALOG_X_MASK 0x2000U

#define JOYSTICK_DATA_BTN_POS 0U
#define JOYSTICK_DATA_BTN_IDR_POS 7U

#define JOYSTICK_DATA_SPECIAL_BTN_POS 8U
#define JOYSTICK_DATA_SPECIAL_BTN_IDR_POS 11U
// 8 buttons dpad - 8 bits
// 2 special buttons - 2 bits
// 4 axes = 4 bits
// [0 0 L_AnalogX L_AnalogY R_AnalogX R_AnalogY Special1 Special2] [L_DLeft L_DDown L_DRight L_DUp R_DLeft R_DDown R_DRight R_DUp]
volatile uint16_t g_joystick_data = 0U;

void joystickReadData(void)
{
    // Clear the dpad and special buttons
    g_joystick_data &= ~(JOYSTICK_DATA_BTN_MASK | JOYSTICK_DATA_SPECIAL_BTN_MASK);

    // Set dpad buttons
    // Optimised like this due to input pins being in consecutive order starting from
    // PE7 to PE14
    g_joystick_data |= (~((GPIOE->IDR >> JOYSTICK_DATA_BTN_IDR_POS) << JOYSTICK_DATA_BTN_POS) & JOYSTICK_DATA_BTN_MASK);

    // Set special buttons
    // Optimised like this due to input pins being in consecutive order starting from
    // PB11 to PB12
    g_joystick_data |= (~((GPIOB->IDR >> JOYSTICK_DATA_SPECIAL_BTN_IDR_POS) << JOYSTICK_DATA_SPECIAL_BTN_POS) & JOYSTICK_DATA_SPECIAL_BTN_MASK);
}

void joystickInit(void)
{
    g_joystick_data = 0U;
    // TODO use ADC to read the analog joysticks
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
bool joystickGetRAnalogY(void)
{
    return (g_joystick_data & JOYSTICK_DATA_R_ANALOG_Y_MASK) != 0U;
}
bool joystickGetRAnalogX(void)
{
    return (g_joystick_data & JOYSTICK_DATA_R_ANALOG_X_MASK) != 0U;
}
bool joystickGetLAnalogY(void)
{
    return (g_joystick_data & JOYSTICK_DATA_L_ANALOG_Y_MASK) != 0U;
}
bool joystickGetLAnalogX(void)
{
    return (g_joystick_data & JOYSTICK_DATA_L_ANALOG_X_MASK) != 0U;
}
