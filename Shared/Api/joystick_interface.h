#ifndef __JOYSTICK_API_H
#define __JOYSTICK_API_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Input types shared by the console and loaded games.
 *
 * Both the console menus and a loaded game read the whole pad in one call:
 * inputGetState(&in) (games) / joystickGetState(&in) (console) fills an InputState.
 * Each button carries its own held / pressed / released flags, so a caller reads
 * e.g. in.special1.pressed with no bit math. "pressed"/"released" are the
 * rising/falling edges since the previous frame, latched once per frame at the
 * frame seam, so callers get btn/btnp semantics for free.
 */

/* One button's state for the frame. `held` is the current level; `pressed` and
 * `released` are the rising/falling edges since the previous frame. */
typedef struct InputButtonState
{
    bool held;
    bool pressed;
    bool released;
} InputButtonState;

/* Full pad snapshot for one frame: the ten buttons (two 4-way d-pads + two
 * special buttons), then the analog axes. Axes are centered and deadzoned to
 * -512..+512 (up / right read positive); the rest position and a small deadzone
 * read as 0, so a game can use the value directly without its own calibration. */
typedef struct InputState
{
    InputButtonState r_up;    /* right d-pad */
    InputButtonState r_right;
    InputButtonState r_down;
    InputButtonState r_left;
    InputButtonState l_up;    /* left d-pad */
    InputButtonState l_right;
    InputButtonState l_down;
    InputButtonState l_left;
    InputButtonState special1; /* select / confirm / toggle */
    InputButtonState special2; /* back / quit */
    int16_t left_x;            /* left stick  X, -512 (left) .. +512 (right) */
    int16_t left_y;            /* left stick  Y, -512 (down) .. +512 (up)    */
    int16_t right_x;           /* right stick X, -512 (left) .. +512 (right) */
    int16_t right_y;           /* right stick Y, -512 (down) .. +512 (up)    */
} InputState;

#endif /* __JOYSTICK_API_H */
