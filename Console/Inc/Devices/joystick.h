#ifndef __JOYSTICK_H
#define __JOYSTICK_H
#include <stdint.h>
#include <stdbool.h>
#include "joystick_interface.h"
void joystickInit(void);
void joystickReadData(void);

/* Latch one frame of input: snapshot the debounced button state, derive the
 * pressed/released edges against the previous latch, and sample the analog axes.
 * The owner of a frame loop calls this once per frame (the kernel at the game
 * frame seam, the menus at their poll), then reads the result with
 * joystickGetState(). Edges are relative to the previous latch, so a button held
 * across a screen/game transition fires "pressed" exactly once. */
void joystickPollFrame(void);

/* Copy the most recently latched frame snapshot. */
void joystickGetState(InputState *out);

#endif /* __JOYSTICK_H */