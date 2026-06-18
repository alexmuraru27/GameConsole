#ifndef __JOYSTICK_API_H
#define __JOYSTICK_API_H

/*
 * Joystick types shared by the console and loaded games. The analog axes report a
 * 3-state result rather than a raw value, so games (and the console) handle the
 * same typed enum instead of a bare integer.
 */

typedef enum JoystickAxisState
{
    JoystickAxisStateOff = 0U,
    JoystickAxisStateNegative = 1U,
    JoystickAxisStatePositive = 2U,
} JoystickAxisState;

#endif /* __JOYSTICK_API_H */
