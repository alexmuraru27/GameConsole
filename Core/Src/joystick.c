#include "joystick.h"
#include "adc.h"

#define JOYSTICK_DATA_BTN_R_MASK 0x000FU
#define JOYSTICK_DATA_BTN_R_DUP_MASK 0x0001U
#define JOYSTICK_DATA_BTN_R_DRIGHT_MASK 0x0002U
#define JOYSTICK_DATA_BTN_R_DDOWN_MASK 0x0004U
#define JOYSTICK_DATA_BTN_R_DLEFT_MASK 0x0008U

#define JOYSTICK_DATA_BTN_L_MASK 0x00F0U
#define JOYSTICK_DATA_BTN_L_DUP_MASK 0x0010U
#define JOYSTICK_DATA_BTN_L_DRIGHT_MASK 0x0020U
#define JOYSTICK_DATA_BTN_L_DDOWN_MASK 0x0040U
#define JOYSTICK_DATA_BTN_L_DLEFT_MASK 0x0080U

#define JOYSTICK_DATA_SPECIAL_BTN_MASK 0x0300U
#define JOYSTICK_DATA_SPECIAL_BTN_1_MASK 0x0100U
#define JOYSTICK_DATA_SPECIAL_BTN_2_MASK 0x0200U

#define JOYSTICK_DATA_ANALOG_MASK 0x3C00U
#define JOYSTICK_DATA_R_ANALOG_Y_MASK 0x0400U
#define JOYSTICK_DATA_R_ANALOG_X_MASK 0x0800U
#define JOYSTICK_DATA_L_ANALOG_Y_MASK 0x1000U
#define JOYSTICK_DATA_L_ANALOG_X_MASK 0x2000U

// 8 buttons dpad - 8 bits
// 2 special buttons - 2 bits
// 4 axes - 4x4bits = 16 bits
// ------ total 26 bits
// uint16 -> [0 0 L_AnalogX L_AnalogY R_AnalogX R_AnalogY Special1 Special2] [L_DLeft L_DDown L_DRight L_DUp R_DLeft R_DDown R_DRight R_DUp]
volatile uint16_t g_joystick_data = 0U;

// TIMX_IRQHandler()
// {
// Clear the dpad buttons
// g_joystick_data &= ~JOYSTICK_DATA_BTN_MASK;
// g_joystick_data |= GPIOE->IDR data;

// Clear special buttons
// g_joystick_data &= ~JOYSTICK_DATA_SPECIAL_BTN_MASK;
// g_joystick_data |= GPIOB->IDR data;
// }

void joystickInit(void)
{
    g_joystick_data = 0U;
    // TODO Use timer to read analog inputs via interrupt
    // TODO use ADC to read the analog joysticks
}
