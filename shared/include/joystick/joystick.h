#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f407xx.h"
typedef enum
{
    JOYSTICK_HORIZONTAL_LEFT = 1,
    JOYSTICK_HORIZONTAL_RIGHT = 2,
    JOYSTICK_HORIZONTAL_CENTERED = 3
} JOYSTICK_HORIZONTAL;

typedef enum
{
    JOYSTICK_VERTICAL_UP = 1,
    JOYSTICK_VERTICAL_DOWN = 2,
    JOYSTICK_VERTICAL_CENTERED = 3
} JOYSTICK_VERTICAL;

typedef enum
{
    JOYSTICK_SWITCH_OFF = 0,
    JOYSTICK_SWITCH_ON = 1,
} JOYSTICK_SWITCH;

void joystick_init(void);
JOYSTICK_HORIZONTAL get_joystick_x(void);
JOYSTICK_VERTICAL get_joystick_y(void);
JOYSTICK_SWITCH get_joystick_switch(void);

#endif /* __JOYSTICK_H */