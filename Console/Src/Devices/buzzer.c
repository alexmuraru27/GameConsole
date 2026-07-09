#include "buzzer.h"
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "sysclock.h" /* APB1_TIMER_CLK_HZ for the PWM period */
#include <stddef.h>
#include "logger.h"

/* ---- TIM3_CH2 PWM output stage (PB5) ----
 * The buzzer's single square-wave voice. Owned here (not in the generic timer
 * module) so the whole driver — arbitration + the pin it drives — lives together. */

static void timer3Init(void)
{
    // Defaults to 1ms
    TIM3->CR1 = 0U;                                                                        // Reset control register
    TIM3->PSC = (APB1_TIMER_CLK_HZ / 1000000U) - 1U;                                       // 1 MHz tick (1 µs)
    TIM3->ARR = 1000U;                                                                     // Default period
    TIM3->CCR2 = 500U;                                                                     // 50% duty cycle for Channel 2
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC2M) | (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1); // PWM mode 1 for Channel 2
    TIM3->CCER |= TIM_CCER_CC2E;                                                           // Enable channel 2 output
}

static void timer3Trigger(uint32_t frequency_hz)
{
    // calculate PWM period for frequency: cycles = (APB1_TIMER_CLK_HZ / frequency_hz)
    uint32_t arr = APB1_TIMER_CLK_HZ / frequency_hz;
    // ensure valid period (arr should be at least 2)
    if (arr < 2)
        arr = 2;
    // adjust prescaler for low frequencies to improve precision
    uint32_t psc = 0;
    while (arr > 65535)
    {
        // TIM3 ARR is 16-bit
        psc++;
        arr = APB1_TIMER_CLK_HZ / (frequency_hz * (psc + 1));
    }

    TIM3->PSC = psc;
    TIM3->ARR = arr - 1U;
    TIM3->CCR2 = arr / 2U; // 50% duty (square wave)
    // Restore PWM mode 1 (timer3Disable forces the output to inactive/low when idle)
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC2M) | (TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1);
    TIM3->CR1 |= TIM_CR1_CEN;
}

static void timer3Disable(void)
{
    // Stop the counter, then force the output compare to its inactive (low) level.
    // Just clearing CEN freezes the counter and leaves PB5 driven at whatever the last
    // comparison produced — which can be a constant 3.3V. The buzzer pin is AF push-pull,
    // so that DC bias keeps current flowing through the passive buzzer and heats it up.
    // OC2M = 0b100 (force inactive) drives OC2REF low independent of the counter, so with
    // active-high polarity (CC2P = 0) PB5 is actively held at 0V while idle.
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC2M) | TIM_CCMR1_OC2M_2;
}

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
} TrackData;

static TrackData s_track_data_queue[SOUND_TRACKS];
static bool s_muted = false;

uint8_t buzzerGetMaxTracks()
{
    return SOUND_TRACKS;
}

static bool clearTrack(const uint8_t track_number);

void buzzerInit(void)
{
    timer3Init(); /* bring up the TIM3_CH2 PWM output stage this driver owns */
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        clearTrack(track_id);
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
 * advance, pause/resume, stop, mute) routes through here, so arbitration lives in
 * exactly one place. */
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
                    timer3Trigger(frequency_hz);
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

    TrackData *const t = &s_track_data_queue[track_id];
    if (t->note_idx >= t->notes)
    {
        if (t->is_looped)
        {
            signalDone(track_id);
            t->note_idx = 0U;
        }
        else
        {
            buzzerStop(track_id); /* clears the track and refreshes the output */
            return;
        }
    }

    t->ms_counter = 0U;
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

void buzzerInterruptHandler(void)
{
    // TIM6 1ms interrupt to update the queue
    // We play certain notes for more than 1ms,so 1ms should be more than enough distance between notes
    for (uint8_t track_id = 0U; track_id < SOUND_TRACKS; track_id++)
    {
        TrackData *const t = &s_track_data_queue[track_id];
        if (!t->is_playing)
        {
            continue;
        }

        if (t->note_idx >= t->notes)
        {
            buzzerStop(track_id);
            continue;
        }

        t->ms_counter++;
        if (t->ms_counter >= t->notes_data[t->note_idx * 2U + 1U])
        {
            t->note_idx++;
            updatePWM(track_id);
        }
    }
}

bool buzzerPlayWithFlag(const uint8_t track_number, const bool is_looped, const uint16_t *const notes_data, const uint16_t notes, bool *on_done_flag)
{
    if (track_number < SOUND_TRACKS && notes_data != NULL && notes != 0U)
    {
        TrackData *const t = &s_track_data_queue[track_number];
        clearTrack(track_number);
        t->notes_data = notes_data;
        t->notes = notes;
        t->on_done_flag = on_done_flag;
        t->is_looped = is_looped;
        t->is_playing = true;
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