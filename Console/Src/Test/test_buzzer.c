#include "test_buzzer.h"
#include "buzzer.h"
#include "usart.h"

uint16_t melody[] = {
    NOTE_E7, NOTE_E7, 0, NOTE_E7,
    0, NOTE_C7, NOTE_E7, 0,
    NOTE_G7, 0, 0, 0,
    NOTE_G6, 0, 0, 0,

    NOTE_C7, 0, 0, NOTE_G6,
    0, 0, NOTE_E6, 0,
    0, NOTE_A6, 0, NOTE_B6,
    0, NOTE_AS6, NOTE_A6, 0,

    NOTE_G6, NOTE_E7, NOTE_G7,
    NOTE_A7, 0, NOTE_F7, NOTE_G7,
    0, NOTE_E7, 0, NOTE_C7,
    NOTE_D7, NOTE_B6, 0, 0,

    NOTE_C7, 0, 0, NOTE_G6,
    0, 0, NOTE_E6, 0,
    0, NOTE_A6, 0, NOTE_B6,
    0, NOTE_AS6, NOTE_A6, 0,

    NOTE_G6, NOTE_E7, NOTE_G7,
    NOTE_A7, 0, NOTE_F7, NOTE_G7,
    0, NOTE_E7, 0, NOTE_C7,
    NOTE_D7, NOTE_B6, 0, 0};

uint16_t tempo[] = {
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    90,
    90,
    90,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    90,
    90,
    90,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
    120,
};

uint16_t underworld_melody[] = {
    NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
    NOTE_AS3, NOTE_AS4, 0,
    0,
    NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
    NOTE_AS3, NOTE_AS4, 0,
    0,
    NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
    NOTE_DS3, NOTE_DS4, 0,
    0,
    NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
    NOTE_DS3, NOTE_DS4, 0,
    0, NOTE_DS4, NOTE_CS4, NOTE_D4,
    NOTE_CS4, NOTE_DS4,
    NOTE_DS4, NOTE_GS3,
    NOTE_G3, NOTE_CS4,
    NOTE_C4, NOTE_FS4, NOTE_F4, NOTE_E3, NOTE_AS4, NOTE_A4,
    NOTE_GS4, NOTE_DS4, NOTE_B3,
    NOTE_AS3, NOTE_A3, NOTE_GS3,
    0, 0, 0};

uint16_t underworld_tempo[] = {
    120, 120, 120, 120,
    120, 120, 60,
    30,
    120, 120, 120, 120,
    120, 120, 60,
    30,
    120, 120, 120, 120,
    120, 120, 60,
    30,
    120, 120, 120, 120,
    120, 120, 60,
    60, 180, 180, 180,
    60, 60,
    60, 60,
    60, 60,
    180, 180, 180, 180, 180, 180,
    100, 100, 100,
    100, 100, 100,
    30, 30, 3};

uint16_t explosion_notes[] = {
    1000U, 800U, 600U, 500U, 400U, 300U, 200U, 150U, 100U};

uint16_t explosion_durations[] = {
    10U, 10U, 15U, 15U, 20U, 20U, 30U, 40U, 50U};

void track0FinishedCallback()
{
    debugString("track0FinishedCallback\r\n");
}

bool isTrack1Playing = false;
void track1FinishedCallback()
{
    isTrack1Playing = false;
    debugString("track1FinishedCallback\r\n");
}

void testBuzzerTrack0(void)
{
    buzzerPlayWithCallback(0, true, melody, tempo, sizeof(melody) / sizeof(uint16_t), &track0FinishedCallback);
}
void testBuzzerTrack1(void)
{
    if (!isTrack1Playing)
    {
        isTrack1Playing = true;
        buzzerPlayWithCallback(1, false, explosion_notes, explosion_durations, sizeof(explosion_notes) / sizeof(uint16_t), &track1FinishedCallback);
    }
}
void testBuzzer(void)
{
}