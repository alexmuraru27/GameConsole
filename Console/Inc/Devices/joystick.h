#ifndef __JOYSTICK_H
#define __JOYSTICK_H
#include <stdint.h>
#include <stdbool.h>
#include "joystick_interface.h"
void joystickInit(void);
void joystickReadData(void);

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
JoystickAxisState joystickGetRAnalogY(void);
JoystickAxisState joystickGetRAnalogX(void);
JoystickAxisState joystickGetLAnalogY(void);
JoystickAxisState joystickGetLAnalogX(void);
bool joystickIsAnyButtonPressed(void);

#endif /* __JOYSTICK_H */