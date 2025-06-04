#include "buzzer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "timer.h"
#include "stddef.h"

#define SOUND_TRACKS 3
#define INVALID_TRACK 255
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

static bool clearNotes(const uint8_t track_number)
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

static bool isOtherTrackPlaying(const uint8_t track_number)
{
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        if (s_is_playing[track_id] && track_id != track_number)
        {
            return true;
        }
    }
    return false;
}

static uint8_t getLastTrackPlaying()
{
    // Returns the last track that is found to be playing
    // Defaults to INVALID_TRACK
    for (uint8_t track_id = SOUND_TRACKS; track_id > 0U; track_id--)
    {
        if (s_is_playing[track_id - 1U])
        {
            return track_id - 1U;
        }
    }

    return INVALID_TRACK;
}

static void updatePWM(uint8_t track_id)
{
    if (track_id < SOUND_TRACKS)
    {
        if (s_note_idx[track_id] >= s_track_data_queue[track_id].notes)
        {
            buzzerStop(track_id);
            return;
        }

        // safe to set to 0 here because updatePWM is called only when the current song is playing
        // after the current note duration was exceeded
        // it should be set here in order to let the background songs to be playing
        s_ms_counter[track_id] = 0;
        // highest track IDs have priority in playing
        if (track_id == getLastTrackPlaying())
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

bool buzzerUnSetCallback(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_buzzer_on_done_callback[track_number] = NULL;
        return true;
    }
    return false;
}

bool buzzerPause(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_is_playing[track_number] = false;
        if (!isOtherTrackPlaying(track_number))
        {
            timer3Disable();
        }
        return true;
    }
    return false;
}

bool buzzerStop(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        if (!isOtherTrackPlaying(track_number))
        {
            timer3Disable();
        }
        clearNotes(track_number);
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

bool buzzerPlay(const uint8_t track_number, uint16_t *const frequencies, uint16_t *const durations_ms, const uint16_t notes)
{
    if (track_number < SOUND_TRACKS && frequencies != NULL && durations_ms != NULL && notes != 0U)
    {
        clearNotes(track_number);
        s_track_data_queue[track_number].frequencies_hz = frequencies;
        s_track_data_queue[track_number].durations_ms = durations_ms;
        s_track_data_queue[track_number].notes = notes;
        s_is_playing[track_number] = true;
        return true;
    }
    return false;
}