#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f407xx.h"
#include "shared_api/include/joystick_driver_api.h"

void joystick_init(void);
JOYSTICK_HORIZONTAL get_joystick_x(void);
JOYSTICK_VERTICAL get_joystick_y(void);
JOYSTICK_SWITCH get_joystick_switch(void);

#endif /* __JOYSTICK_H */