#include "buzzer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "timer.h"
#include "stddef.h"

#define MAX_NOTES 100
typedef struct
{
    uint16_t frequency_hz; // note frequency in hz (0 for silence)
    uint16_t duration_ms;  // duration in milliseconds
} Note;

static Note s_note_queue[MAX_NOTES];
static uint32_t s_queue_head = 0;  // Index to add new notes
static uint32_t s_queue_tail = 0;  // Index of current note being played
static uint32_t s_ms_counter = 0;  // Counts milliseconds for current note
static uint32_t s_duration_ms = 0; // Current note duration in milliseconds
static bool s_is_playing = false;  // Playback status
static void (*s_buzzer_on_done_callback)(void) = NULL;

void buzzerSetCallback(void (*onDone)(void))
{
    s_buzzer_on_done_callback = onDone;
}

void buzzerPause(void)
{
    // TODO
}
void buzzerStop(void)
{
    // TODO
}

void buzzerInit(void)
{
    s_queue_head = 0U;
    s_queue_tail = 0U;
    s_ms_counter = 0U;
    s_duration_ms = 0U;
    s_is_playing = false;
    s_buzzer_on_done_callback = NULL;
}

bool buzzerAddNote(const uint16_t frequency_hz, const uint16_t s_duration_ms)
{
    if (s_queue_head >= MAX_NOTES)
    {
        return false; // Queue full
    }
    s_note_queue[s_queue_head].frequency_hz = frequency_hz;
    s_note_queue[s_queue_head].duration_ms = s_duration_ms;
    s_queue_head++;
    return true;
}

void buzzerClearNotes()
{
    timer3Disable();
    s_queue_head = 0U;
    s_queue_tail = 0U;
    s_ms_counter = 0U;
    s_duration_ms = 0U;
    s_is_playing = false;
}

static void updatePWM(void)
{
    // update PWM for the current note
    if (s_queue_tail >= s_queue_head)
    {
        timer3Disable();
        s_is_playing = false;
        if (s_buzzer_on_done_callback != NULL)
        {
            s_buzzer_on_done_callback();
        }
        return;
    }

    const uint32_t frequency_hz = s_note_queue[s_queue_tail].frequency_hz;
    s_duration_ms = s_note_queue[s_queue_tail].duration_ms;
    s_ms_counter = 0;

    if (frequency_hz == 0)
    {
        timer3Disable();
    }
    else
    {
        timer3Trigger(frequency_hz, 50U);
    }
}

void buzzerPlay(void)
{
    if (s_queue_tail < s_queue_head)
    {
        s_is_playing = true;
        updatePWM();
    }
}

void buzzerInterruptHandler(void)
{
    // TIM6 1ms interrupt to update the queue
    // We play certain notes for more than 1ms,so 1ms should be more than enough distance between notes
    if (!s_is_playing)
        return;

    s_ms_counter++;
    if (s_ms_counter >= s_duration_ms)
    {
        s_queue_tail++;
        updatePWM();
    }
}

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
void buzzerPlayMelody(void)
{
    // TODO Make this play from user buffer
    s_queue_head = 0;
    s_queue_tail = 0;

    for (uint16_t i = 0; i < sizeof(melody) / sizeof(uint16_t); i++)
    {
        buzzerAddNote(melody[i], tempo[i]);
    }

    buzzerPlay();
}