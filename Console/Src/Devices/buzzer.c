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
    const uint16_t *notes_data; // interleaved {freq_hz, duration_ms, freq_hz, duration_ms, ...}
    uint16_t notes;             // Number of notes
    bool is_playing;            // Playback status
    bool is_looped;             // If the track should be looped
    bool *on_done_flag;         // Set to true by the ISR when the track finishes
    uint32_t note_idx;          // Index of current note being played
    uint32_t ms_counter;        // Counts milliseconds for current note
} TrackData;

static CCMRAM TrackData s_track_data_queue[SOUND_TRACKS];
static bool s_muted = false;

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
        s_track_data_queue[track_id].notes_data = NULL;
        s_track_data_queue[track_id].notes = 0U;
        s_track_data_queue[track_id].on_done_flag = NULL;
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
        s_track_data_queue[track_number].notes_data = NULL;
        s_track_data_queue[track_number].notes = 0U;
        s_track_data_queue[track_number].on_done_flag = NULL;
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

static void signalDone(uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        if (s_track_data_queue[track_number].on_done_flag != NULL)
        {
            *s_track_data_queue[track_number].on_done_flag = true;
        }
    }
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
                signalDone(track_id);
                s_track_data_queue[track_id].note_idx = 0U;
            }
            else
            {
                // If it's not looped, just stop it
                buzzerStop(track_id);
                return;
            }
        }

        s_track_data_queue[track_id].ms_counter = 0;

        // highest track IDs have priority in playing
        if (track_id == getLastTrackPlaying())
        {
            const uint32_t frequency_hz = s_track_data_queue[track_id].notes_data[s_track_data_queue[track_id].note_idx * 2U];

            if (s_muted || frequency_hz == 0)
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
        signalDone(track_number);
        clearTrack(track_number);
        return true;
    }
    return false;
}

void buzzerStopAll()
{
    for (uint8_t track_number = 0U; track_number < SOUND_TRACKS; track_number++)
    {
        buzzerStop(track_number);
    }
}

bool buzzerResume(uint8_t track_number)
{
    if (track_number < SOUND_TRACKS && s_track_data_queue[track_number].notes_data != NULL)
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

        if (s_track_data_queue[track_id].note_idx >= s_track_data_queue[track_id].notes)
        {
            buzzerStop(track_id);
            continue;
        }

        s_track_data_queue[track_id].ms_counter++;
        if (s_track_data_queue[track_id].ms_counter >= s_track_data_queue[track_id].notes_data[s_track_data_queue[track_id].note_idx * 2U + 1U])
        {
            s_track_data_queue[track_id].note_idx++;
            updatePWM(track_id);
        }
    }
}

bool buzzerPlayWithFlag(const uint8_t track_number, const bool is_looped, const uint16_t *const notes_data, const uint16_t notes, bool *on_done_flag)
{
    if (track_number < SOUND_TRACKS && notes_data != NULL && notes != 0U)
    {
        clearTrack(track_number);
        s_track_data_queue[track_number].notes_data = notes_data;
        s_track_data_queue[track_number].notes = notes;
        s_track_data_queue[track_number].on_done_flag = on_done_flag;
        s_track_data_queue[track_number].is_looped = is_looped;
        s_track_data_queue[track_number].is_playing = true;
        updatePWM(track_number);
        return true;
    }
    return false;
}

bool buzzerPlay(const uint8_t track_number, const bool is_looped, const uint16_t *const notes_data, const uint16_t notes)
{
    return buzzerPlayWithFlag(track_number, is_looped, notes_data, notes, NULL);
}

void buzzerSetMute(const bool muted)
{
    s_muted = muted;
    if (muted)
    {
        timer3Disable();
    }
    else
    {
        // Restore PWM for the current note on the highest-priority active track
        uint8_t track_id = getLastTrackPlaying();
        if (track_id != INVALID_TRACK)
        {
            const uint32_t freq = s_track_data_queue[track_id].notes_data[s_track_data_queue[track_id].note_idx * 2U];
            if (freq != 0)
            {
                timer3Trigger(freq, 50U);
            }
        }
    }
}

bool buzzerIsMuted(void)
{
    return s_muted;
}