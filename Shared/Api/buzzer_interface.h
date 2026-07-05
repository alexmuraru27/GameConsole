#ifndef __BUZZER_API_H
#define __BUZZER_API_H

/*
 * Buzzer constants shared by the console and loaded games.
 *
 * The buzzer is a single PWM channel, so "timbre" is just the pulse duty cycle:
 * 50% is a full, hollow square; the further from 50% the thinner/reedier the tone
 * (the classic chiptune trick for faking instruments on one square-wave voice).
 * buzzerSetTimbre() takes a duty percent in [BUZZER_DUTY_MIN, BUZZER_DUTY_MAX]
 * (out-of-range is rejected — it returns false); each track keeps its own timbre,
 * sticky across plays until changed, defaulting to a square wave.
 *
 * The named values below are just convenient presets — pass any number in range.
 */
#define BUZZER_DUTY_MIN 10U
#define BUZZER_DUTY_MAX 90U

#define BUZZER_TIMBRE_SQUARE 50U /* full, hollow square (default) */
#define BUZZER_TIMBRE_PULSE 25U  /* brighter, reedier pulse       */
#define BUZZER_TIMBRE_THIN 12U   /* thin, nasal pulse             */

#endif /* __BUZZER_API_H */
