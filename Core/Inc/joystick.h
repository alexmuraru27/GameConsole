#ifndef __JOYSTICK_H
#define __JOYSTICK_H
#include <stdint.h>
#include <stdbool.h>
void joystickInit(void);
void joystickReadData(void);

typedef enum JoystickAnalogValue
{
    JoystickAnalogValueOff = 0U,
    JoystickAnalogValueLowAxis = 1U,
    JoystickAnalogValueHighAxis = 2U
} JoystickAnalogValue;

bool joystickGetRBtnUp(void);
bool joystickGetRBtnRight(void);
bool joystickGetRBtnDown(void);
bool joystickGetRBtnLeft(void);
bool joystickGetLBtnUp(void);
bool joystickGetLBtnRight(void);
bool joystickGetLBtnDown(void);
bool joystickGetLBtnLeft(void);
bool joystickGetSpecialBtn1(void);
bool joystickGetSpecialBtn2(void);
JoystickAnalogValue joystickGetRAnalogY(void);
JoystickAnalogValue joystickGetRAnalogX(void);
JoystickAnalogValue joystickGetLAnalogY(void);
JoystickAnalogValue joystickGetLAnalogX(void);

#endif /* __JOYSTICK_H */