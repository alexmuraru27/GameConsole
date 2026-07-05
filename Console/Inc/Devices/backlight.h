#ifndef __BACKLIGHT_H
#define __BACKLIGHT_H

#include <stdint.h>

/*
 * Display backlight brightness. PA3 drives the panel backlight; it is muxed to
 * TIM9_CH2 (AF3) by gpioInit and driven here as a PWM whose duty cycle sets the
 * brightness. Brightness is a whole percentage, snapped to BACKLIGHT_STEP_PERCENT
 * and floored at BACKLIGHT_MIN_PERCENT so the screen never goes fully dark.
 */

#define BACKLIGHT_MIN_PERCENT 10U     /* never fully off — keep the panel visible */
#define BACKLIGHT_MAX_PERCENT 100U
#define BACKLIGHT_STEP_PERCENT 10U    /* the Settings slider moves in 10% steps    */
#define BACKLIGHT_DEFAULT_PERCENT 100U

/* Bring up TIM9_CH2 PWM on PA3 and drive the panel at the default brightness.
 * PA3 must already be muxed to AF3 (done in gpioInit). Call once at boot. */
void backlightInit(void);

/* Set the backlight to `percent`, clamped to [MIN,MAX] and snapped to a step.
 * Takes effect immediately; persistence is the caller's concern. */
void backlightSetBrightness(uint8_t percent);

/* The current brightness percentage (already clamped/snapped). */
uint8_t backlightGetBrightness(void);

#endif /* __BACKLIGHT_H */
