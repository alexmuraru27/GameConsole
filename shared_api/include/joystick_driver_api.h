#ifndef __JOYSTICK_DRIVER_API_H
#define __JOYSTICK_DRIVER_API_H

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

#endif /*__JOYSTICK_DRIVER_API_H*/