#ifndef __BUZZER_H
#define __BUZZER_H
#include <stdint.h>
#include "stdbool.h"

// in milliherz (or multiplied by 1000)
#define NOTE_PAUSE 0U
#define NOTE_C3 130U
#define NOTE_CS3 138U
#define NOTE_D3 146U
#define NOTE_DS3 155U
#define NOTE_E3 164U
#define NOTE_F3 174U
#define NOTE_FS3 184U
#define NOTE_G3 195U
#define NOTE_GS3 207U
#define NOTE_A3 220U
#define NOTE_AS3 233U
#define NOTE_B3 246U
#define NOTE_C4 261U
#define NOTE_CS4 277U
#define NOTE_D4 293U
#define NOTE_DS4 311U
#define NOTE_E4 329U
#define NOTE_F4 349U
#define NOTE_FS4 369U
#define NOTE_G4 391U
#define NOTE_GS4 415U
#define NOTE_A4 440U
#define NOTE_AS4 466U
#define NOTE_B4 493U
#define NOTE_C5 523U
#define NOTE_CS5 554U
#define NOTE_D5 587U
#define NOTE_DS5 622U
#define NOTE_E5 659U
#define NOTE_F5 698U
#define NOTE_FS5 739U
#define NOTE_G5 783U
#define NOTE_GS5 830U
#define NOTE_A5 880U
#define NOTE_AS5 932U
#define NOTE_B5 987U
#define NOTE_C6 1046U
#define NOTE_CS6 1109U
#define NOTE_D6 1175U
#define NOTE_DS6 1245U
#define NOTE_E6 1319U
#define NOTE_F6 1397U
#define NOTE_FS6 1480U
#define NOTE_G6 1568U
#define NOTE_GS6 1661U
#define NOTE_A6 1760U
#define NOTE_AS6 1865U
#define NOTE_B6 1976U
#define NOTE_C7 2093U
#define NOTE_CS7 2217U
#define NOTE_D7 2349U
#define NOTE_DS7 2489U
#define NOTE_E7 2637U
#define NOTE_F7 2794U
#define NOTE_FS7 2960U
#define NOTE_G7 3136U
#define NOTE_GS7 3322U
#define NOTE_A7 3520U
#define NOTE_AS7 3729U
#define NOTE_B7 3951U
#define NOTE_C8 4186U
#define NOTE_CS8 4435U
#define NOTE_D8 4699U
#define NOTE_DS8 4978U

void buzzerInit(void);
void buzzerClearNotes(void);
void buzzerPlayMelody(void);
void buzzerPause(void);
void buzzerStop(void);
bool buzzerAddNote(uint16_t frequency_hz, uint16_t s_duration_ms);
void buzzerSetCallback(void (*onDone)(void));
void buzzerInterruptHandler(void);
#endif /* __BUZZER_H */