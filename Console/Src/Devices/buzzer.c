#include "buzzer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "timer.h"
#include "stddef.h"

#define CCMRAM __attribute__((section(".ccmram")))

#define SOUND_TRACKS 5
#define INVALID_TRACK 255
typedef struct
{
    uint16_t *frequencies_hz; // note frequency in hz (0 for silence)
    uint16_t *durations_ms;   // duration in milliseconds
    uint16_t notes;           // Number of notes
    bool is_playing;          // Playback status
    bool is_looped;           // If the track should be looped
    void (*callback)(void);   // Callback fptr
    uint32_t note_idx;        // Index of current note being played
    uint32_t ms_counter;      // Counts milliseconds for current note
} TrackData;

static CCMRAM TrackData s_track_data_queue[SOUND_TRACKS];

uint8_t buzzerGetMaxTracks()
{
    return SOUND_TRACKS;
}

void buzzerInit(void)
{
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        s_track_data_queue[track_id].is_looped = false;
        s_track_data_queue[track_id].is_playing = false;
        s_track_data_queue[track_id].durations_ms = NULL;
        s_track_data_queue[track_id].frequencies_hz = NULL;
        s_track_data_queue[track_id].notes = 0U;
        s_track_data_queue[track_id].callback = NULL;
        s_track_data_queue[track_id].note_idx = 0U;
        s_track_data_queue[track_id].ms_counter = 0U;
    }
}

static bool clearTrack(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_track_data_queue[track_number].is_looped = false;
        s_track_data_queue[track_number].is_playing = false;
        s_track_data_queue[track_number].durations_ms = NULL;
        s_track_data_queue[track_number].frequencies_hz = NULL;
        s_track_data_queue[track_number].notes = 0U;
        s_track_data_queue[track_number].callback = NULL;
        s_track_data_queue[track_number].note_idx = 0U;
        s_track_data_queue[track_number].ms_counter = 0U;
        return true;
    }
    return false;
}

static bool isOtherTrackPlaying(const uint8_t track_number)
{
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        if (s_track_data_queue[track_id].is_playing && (track_id != track_number))
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
        if (s_track_data_queue[track_id - 1U].is_playing)
        {
            return track_id - 1U;
        }
    }

    return INVALID_TRACK;
}

static bool executeCallback(uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        if (s_track_data_queue[track_number].callback != NULL)
        {
            s_track_data_queue[track_number].callback();
            return true;
        }
    }

    return false;
}

static void updatePWM(uint8_t track_id)
{
    if (track_id < SOUND_TRACKS)
    {
        // If we ended the song
        if (s_track_data_queue[track_id].note_idx >= s_track_data_queue[track_id].notes)
        {
            if (s_track_data_queue[track_id].is_looped)
            {
                // If it's looped -> reset the note index to 0
                executeCallback(track_id);
                s_track_data_queue[track_id].note_idx = 0U;
            }
            else
            {
                // If it's not looped, just stop it
                buzzerStop(track_id);
                return;
            }
        }

        // safe to set to 0 here because updatePWM is called only when the current song is playing
        // after the current note duration was exceeded
        // it should be set here in order to let the background songs to be playing
        s_track_data_queue[track_id].ms_counter = 0;
        // highest track IDs have priority in playing
        if (track_id == getLastTrackPlaying())
        {
            const uint32_t frequency_hz = s_track_data_queue[track_id].frequencies_hz[s_track_data_queue[track_id].note_idx];
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

bool buzzerPause(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_track_data_queue[track_number].is_playing = false;
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
        executeCallback(track_number);
        clearTrack(track_number);
        return true;
    }
    return false;
}

bool buzzerResume(uint8_t track_number)
{
    if (track_number < SOUND_TRACKS && (s_track_data_queue[track_number].durations_ms != NULL) && (s_track_data_queue[track_number].frequencies_hz != NULL))
    {
        s_track_data_queue[track_number].is_playing = true;
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
        if (!s_track_data_queue[track_id].is_playing)
        {
            continue;
        }

        s_track_data_queue[track_id].ms_counter++;
        if (s_track_data_queue[track_id].ms_counter >= s_track_data_queue[track_id].durations_ms[s_track_data_queue[track_id].note_idx])
        {
            s_track_data_queue[track_id].note_idx++;
            updatePWM(track_id);
        }
    }
}

bool buzzerPlayWithCallback(const uint8_t track_number, const bool is_looped, uint16_t *const frequencies, uint16_t *const durations_ms, const uint16_t notes, void (*on_done_callback)(void))
{
    if (track_number < SOUND_TRACKS && frequencies != NULL && durations_ms != NULL && notes != 0U)
    {
        clearTrack(track_number);
        s_track_data_queue[track_number].frequencies_hz = frequencies;
        s_track_data_queue[track_number].durations_ms = durations_ms;
        s_track_data_queue[track_number].notes = notes;
        s_track_data_queue[track_number].callback = on_done_callback;
        s_track_data_queue[track_number].is_looped = is_looped;
        s_track_data_queue[track_number].is_playing = true;
        return true;
    }
    return false;
}

bool buzzerPlay(const uint8_t track_number, const bool is_looped, uint16_t *const frequencies, uint16_t *const durations_ms, const uint16_t notes)
{
    return buzzerPlayWithCallback(track_number, is_looped, frequencies, durations_ms, notes, NULL);
}