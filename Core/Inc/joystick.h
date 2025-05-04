#ifndef __JOYSTICK_H
#define __JOYSTICK_H
#include <stdint.h>
#include <stdbool.h>
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
bool joystickGetRAnalogY(void);
bool joystickGetRAnalogX(void);
bool joystickGetLAnalogY(void);
bool joystickGetLAnalogX(void);

#endif /* __JOYSTICK_H */