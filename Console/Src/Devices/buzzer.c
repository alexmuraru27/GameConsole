#include "buzzer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "timer.h"
#include "stddef.h"

#define SOUND_TRACKS 3
typedef struct
{
    uint16_t *frequencies_hz; // note frequency in hz (0 for silence)
    uint16_t *durations_ms;   // duration in milliseconds
    uint16_t notes;
} TrackData;

static TrackData s_track_data_queue[SOUND_TRACKS];
static uint32_t s_note_idx[SOUND_TRACKS];   // Index of current note being played
static uint32_t s_ms_counter[SOUND_TRACKS]; // Counts milliseconds for current note
static bool s_is_playing[SOUND_TRACKS];     // Playback status
static void (*s_buzzer_on_done_callback[SOUND_TRACKS])(void);

uint8_t buzzerGetMaxTracks()
{
    return SOUND_TRACKS;
}

bool buzzerSetCallback(const uint8_t track_number, void (*onDone)(void))
{
    if (track_number < SOUND_TRACKS)
    {
        s_buzzer_on_done_callback[track_number] = onDone;
        return true;
    }
    return false;
}

bool buzzerPause(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_is_playing[track_number] = false;
        return true;
    }
    return false;
}
bool buzzerStop(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        buzzerClearNotes(track_number);
        if (s_buzzer_on_done_callback[track_number] != NULL)
        {
            s_buzzer_on_done_callback[track_number]();
        }
        return true;
    }
    return false;
}

bool buzzerResume(uint8_t track_number)
{
    if (track_number < SOUND_TRACKS && (s_track_data_queue[track_number].durations_ms != NULL) && (s_track_data_queue[track_number].frequencies_hz != NULL))
    {
        s_is_playing[track_number] = true;
        return true;
    }
    return false;
}
void buzzerInit(void)
{
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        s_track_data_queue[track_id].durations_ms = NULL;
        s_track_data_queue[track_id].frequencies_hz = NULL;
        s_track_data_queue[track_id].notes = 0U;
        s_buzzer_on_done_callback[track_id] = NULL;
        s_note_idx[track_id] = 0U;
        s_ms_counter[track_id] = 0U;
        s_is_playing[track_id] = false;
    }
}

bool buzzerClearNotes(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_is_playing[track_number] = false;
        s_track_data_queue[track_number].durations_ms = NULL;
        s_track_data_queue[track_number].frequencies_hz = NULL;
        s_track_data_queue[track_number].notes = 0U;
        s_note_idx[track_number] = 0U;
        s_ms_counter[track_number] = 0U;
        return true;
    }
    return false;
}

static void updatePWM(uint8_t track_id)
{
    if (s_note_idx[track_id] >= s_track_data_queue[track_id].notes)
    {
        buzzerClearNotes(track_id);
        if (s_buzzer_on_done_callback[track_id] != NULL)
        {
            s_buzzer_on_done_callback[track_id]();
        }
        return;
    }

    // lowest track IDs have priority in playing
    uint8_t first_track_playing = 0U;
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        if (s_is_playing[track_id])
        {
            first_track_playing = track_id;
            break;
        }
    }

    s_ms_counter[track_id] = 0;
    if (track_id == first_track_playing)
    {
        const uint32_t frequency_hz = s_track_data_queue[track_id].frequencies_hz[s_note_idx[track_id]];
        if (frequency_hz == 0)
        {
            timer3Disable();
        }
        else
        {
            timer3Trigger(frequency_hz, 50U);
        }
    }
}

void buzzerInterruptHandler(void)
{
    // TIM6 1ms interrupt to update the queue
    // We play certain notes for more than 1ms,so 1ms should be more than enough distance between notes
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        if (!s_is_playing[track_id])
        {
            continue;
        }

        s_ms_counter[track_id]++;
        if (s_ms_counter[track_id] >= s_track_data_queue[track_id].durations_ms[s_note_idx[track_id]])
        {
            s_note_idx[track_id]++;
            updatePWM(track_id);
        }
    }
}

bool buzzerPlay(uint8_t track_number, uint16_t *frequencies, uint16_t *durations_ms, uint16_t notes)
{
    if (track_number < SOUND_TRACKS)
    {
        buzzerClearNotes(track_number);
        s_track_data_queue[track_number].frequencies_hz = frequencies;
        s_track_data_queue[track_number].durations_ms = durations_ms;
        s_track_data_queue[track_number].notes = notes;
        s_is_playing[track_number] = true;
        return true;
    }
    return false;
}