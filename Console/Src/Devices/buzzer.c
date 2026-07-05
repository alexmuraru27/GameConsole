#include "buzzer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "timer.h"
#include "stddef.h"
#include "logger.h"

#define SOUND_TRACKS 5
typedef struct
{
    const uint16_t *notes_data; // interleaved {freq_hz, duration_ms, freq_hz, duration_ms, ...}
    uint16_t notes;             // Number of notes
    bool is_playing;            // Playback status
    bool is_looped;             // If the track should be looped
    bool *on_done_flag;         // Set to true by the ISR when the track finishes
    uint32_t note_idx;          // Index of current note being played
    uint32_t ms_counter;        // Counts milliseconds for current note
    uint8_t duty;               // PWM duty % (timbre); sticky across plays, default square
} TrackData;

static TrackData s_track_data_queue[SOUND_TRACKS];
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
        s_track_data_queue[track_id].duty = (uint8_t)BUZZER_TIMBRE_SQUARE;
    }
    LOGGER_LOG_INFO(LOGGER_BUZZER, "init: %u tracks", (unsigned)SOUND_TRACKS);
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

/* Drive the single PWM channel from the right track. With one voice, the
 * highest-numbered track wins — but only while it is *audibly* playing: a track
 * that is paused, stopped, or sitting on a rest (freq 0) is skipped so the channel
 * falls through to a lower track that has something to sound, instead of going
 * silent. Muted, or nothing to play -> output off. Every state change (note
 * advance, pause/resume, stop, mute, timbre) routes through here, so arbitration
 * lives in exactly one place. */
static void buzzerRefreshOutput(void)
{
    if (!s_muted)
    {
        for (uint8_t i = SOUND_TRACKS; i > 0U; i--)
        {
            const TrackData *const t = &s_track_data_queue[i - 1U];
            if (t->is_playing && t->note_idx < t->notes)
            {
                const uint16_t frequency_hz = t->notes_data[t->note_idx * 2U];
                if (frequency_hz != 0U)
                {
                    timer3Trigger(frequency_hz, t->duty);
                    return;
                }
            }
        }
    }
    timer3Disable();
}

/* Advance to the note the ISR just stepped onto: loop or stop at the end,
 * otherwise reset the millisecond counter and re-arbitrate the output. */
static void updatePWM(uint8_t track_id)
{
    if (track_id >= SOUND_TRACKS)
    {
        return;
    }

    if (s_track_data_queue[track_id].note_idx >= s_track_data_queue[track_id].notes)
    {
        if (s_track_data_queue[track_id].is_looped)
        {
            signalDone(track_id);
            s_track_data_queue[track_id].note_idx = 0U;
        }
        else
        {
            buzzerStop(track_id); /* clears the track and refreshes the output */
            return;
        }
    }

    s_track_data_queue[track_id].ms_counter = 0U;
    buzzerRefreshOutput();
}

bool buzzerPause(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        s_track_data_queue[track_number].is_playing = false;
        buzzerRefreshOutput(); /* hand the channel to a lower track, don't go silent */
        return true;
    }
    return false;
}

bool buzzerStop(const uint8_t track_number)
{
    if (track_number < SOUND_TRACKS)
    {
        signalDone(track_number);
        clearTrack(track_number);
        buzzerRefreshOutput();
        return true;
    }
    return false;
}

void buzzerStopAll()
{
    LOGGER_LOG_DEBUG(LOGGER_BUZZER, "stop all tracks");
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
        buzzerRefreshOutput(); /* reclaim the channel if it is now the top voice */
        return true;
    }
    return false;
}

bool buzzerSetTimbre(uint8_t track_number, uint8_t duty_percent)
{
    if (track_number >= SOUND_TRACKS ||
        duty_percent < BUZZER_DUTY_MIN || duty_percent > BUZZER_DUTY_MAX)
    {
        LOGGER_LOG_WARN(LOGGER_BUZZER, "timbre rejected: track=%u duty=%u%% (want %u..%u)",
                        (unsigned)track_number, (unsigned)duty_percent,
                        (unsigned)BUZZER_DUTY_MIN, (unsigned)BUZZER_DUTY_MAX);
        return false;
    }
    s_track_data_queue[track_number].duty = duty_percent;
    buzzerRefreshOutput(); /* apply live if this track owns the channel */
    LOGGER_LOG_DEBUG(LOGGER_BUZZER, "timbre track %u -> %u%% duty", (unsigned)track_number, (unsigned)duty_percent);
    return true;
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
        LOGGER_LOG_DEBUG(LOGGER_BUZZER, "play track %u: %u notes, loop=%d", (unsigned)track_number, (unsigned)notes, (int)is_looped);
        return true;
    }
    LOGGER_LOG_WARN(LOGGER_BUZZER, "play rejected: track=%u notes=%u data=%p", (unsigned)track_number, (unsigned)notes, (const void *)notes_data);
    return false;
}

bool buzzerPlay(const uint8_t track_number, const bool is_looped, const uint16_t *const notes_data, const uint16_t notes)
{
    return buzzerPlayWithFlag(track_number, is_looped, notes_data, notes, NULL);
}

void buzzerSetMute(const bool muted)
{
    LOGGER_LOG_INFO(LOGGER_BUZZER, "mute=%d", (int)muted);
    s_muted = muted;
    buzzerRefreshOutput(); /* silences when muted, restores the top voice when unmuted */
}

bool buzzerIsMuted(void)
{
    return s_muted;
}