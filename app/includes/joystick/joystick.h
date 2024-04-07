#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f407xx.h"

void joystick_init(void);
uint16_t get_joystick_x(void);
uint16_t get_joystick_y(void);
uint8_t get_joystick_switch(void);

#endif /* __JOYSTICK_H */