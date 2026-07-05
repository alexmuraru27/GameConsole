#include "game_console_api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * TestBuzzer — a buzzer test harness, shipped as an ordinary loadable game.
 *
 * A companion to TestRenderer: almost no game logic, it just steps through every
 * call in the buzzer ConsoleAPI so you can confirm the synth end to end on real
 * hardware — by ear and on screen. Each step is a self-contained little test that
 *   - performs one buzzer operation (play / pause / resume / stop / loop / with-flag
 *     completion / multi-track arbitration / stop-all),
 *   - draws the operation and its live status on screen, and
 *   - logs the same to SWO on the GAME channel (gameLog).
 * The steps auto-run one after another; Special Button 1 skips to the next
 * immediately, Special Button 2 quits. On the last step SB1 restarts the suite.
 *
 * Audio timing is entirely console-side (the 1 ms TIM6 buzzer ISR advances notes),
 * independent of this game's frame rate, so the game just paces itself with a short
 * delay() and reads the clock; it never needs to be fast.
 */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* Note frequencies in Hz. The console's NOTE_* constants live in a console-only
 * header, so — as a game does — we define the handful we need here. A track is an
 * array of interleaved {frequency_hz, duration_ms} pairs; frequency 0 = a rest. */
#define N_REST 0U
#define N_C3 130U
#define N_C4 261U
#define N_E4 329U
#define N_G4 391U
#define N_A4 440U
#define N_C5 523U
#define N_E5 659U
#define N_G5 783U
#define N_C6 1046U

/* Font-ink palettes (slot 0 transparent, 1-3 the ink colour). */
static const uint16_t PAL_TITLE[4] = {0x0000, 0x07FF, 0x07FF, 0x07FF}; /* cyan   */
static const uint16_t PAL_HEAD[4] = {0x0000, 0xFD20, 0xFD20, 0xFD20};  /* amber  */
static const uint16_t PAL_TEXT[4] = {0x0000, 0xFFFF, 0xFFFF, 0xFFFF};  /* white  */
static const uint16_t PAL_STAT[4] = {0x0000, 0x07E0, 0x07E0, 0x07E0};  /* green  */
static const uint16_t PAL_WARN[4] = {0x0000, 0xF800, 0xF800, 0xF800};  /* red    */
static const uint16_t PAL_DIM[4] = {0x0000, 0x8410, 0x8410, 0x8410};   /* grey   */

/* Test material. */
static const uint16_t MEL_TUNE[] = {N_C4, 150, N_E4, 150, N_G4, 150, N_C5, 260,
                                    N_G4, 150, N_E4, 150, N_C4, 300};
static const uint16_t MEL_LONG[] = {N_A4, 5000};                          /* one sustained tone   */
static const uint16_t MEL_LOOP[] = {N_E5, 130, N_G5, 130, N_C6, 190, N_REST, 150}; /* short motif  */
static const uint16_t MEL_LOW[] = {N_C3, 400};                            /* low, track 0         */
static const uint16_t MEL_HIGH[] = {N_C5, 300};                           /* high, track 1        */
#define PAIRS(a) ((uint16_t)(sizeof(a) / sizeof((a)[0]) / 2U))

typedef enum
{
    PH_INTRO = 0,
    PH_PLAY,
    PH_DONEFLAG,
    PH_PAUSE_RESUME,
    PH_STOP,
    PH_LOOP,
    PH_ARBITRATION,
    PH_STOPALL,
    PH_DONE,
    PH_COUNT
} Phase;

/* Static per-step framing (title + up to two description lines + how long to dwell
 * before auto-advancing; 0 = wait for the player). The dynamic status/count lines
 * are built at run time in the enter/tick handlers below. */
typedef struct
{
    const char *title;
    const char *desc;
    const char *desc2; /* NULL when the step needs only one description line */
    uint32_t dwell_ms;
} PhaseInfo;

static const PhaseInfo PHASES[PH_COUNT] = {
    [PH_INTRO] = {"BUZZER TEST", "Exercises every buzzer API call.", "Each step auto-runs - watch & listen.", 2600U},
    [PH_PLAY] = {"PLAY", "buzzerPlay(): a one-shot melody", "on track 0.", 2400U},
    [PH_DONEFLAG] = {"PLAY + DONE FLAG", "buzzerPlayWithFlag(): the console", "sets a flag when the track ends.", 3200U},
    [PH_PAUSE_RESUME] = {"PAUSE / RESUME", "buzzerPause() then buzzerResume()", "a sustained tone.", 4200U},
    [PH_STOP] = {"STOP", "buzzerStop(): cut a tone short.", NULL, 3000U},
    [PH_LOOP] = {"LOOP", "buzzerPlay(looped=true): repeat a", "short motif until stopped.", 4200U},
    [PH_ARBITRATION] = {"TRACK ARBITRATION", "The single voice sounds the highest", "active track; lower waits.", 6000U},
    [PH_STOPALL] = {"STOP ALL", "buzzerStopAll(): silence every", "track at once.", 3000U},
    [PH_DONE] = {"COMPLETE", "All buzzer functions exercised.", "SB1 restart   SB2 exit.", 0U},
};

/* ---- run-time state ---- */
#define UI_CAP 256U
static Sprite s_ui[UI_CAP];

static Phase s_phase;
static uint32_t s_phase_start;    /* getSysTime() at the current step's entry     */
static uint8_t s_sub;             /* sub-step counter for multi-stage steps       */
static uint8_t s_max_tracks;      /* buzzerGetMaxTracks(), read once at init       */
static volatile bool s_done_flag; /* set by the console when a with-flag track ends */
static bool s_audio_active;       /* drives the blinking on-screen "SND" indicator */

static char s_status[40]; /* the prominent live status line */
static char s_extra[48];  /* an optional second dynamic line (loop count / audible track) */

static void setStatus(const char *s)
{
    snprintf(s_status, sizeof(s_status), "%s", s);
}

/* Total duration of one pass through a note array (sum of the pair durations). */
static uint32_t notesLenMs(const uint16_t *notes, uint16_t pairs)
{
    uint32_t total = 0U;
    for (uint16_t i = 0U; i < pairs; i++)
    {
        total += notes[i * 2U + 1U];
    }
    return total;
}

/* Perform a step's action once, on entry: reset the synth to a clean slate, do the
 * one operation the step demonstrates, seed its status line, and log it. */
static void phaseEnter(Phase p)
{
    buzzerStopAll(); /* isolate each step from the last */
    s_sub = 0U;
    s_done_flag = false;
    s_audio_active = false;
    s_status[0] = '\0';
    s_extra[0] = '\0';

    switch (p)
    {
    case PH_INTRO:
        snprintf(s_status, sizeof(s_status), "buzzerGetMaxTracks() = %u", (unsigned)s_max_tracks);
        gameLog("[buzzer-test] intro: %u tracks available", (unsigned)s_max_tracks);
        break;

    case PH_PLAY:
        buzzerPlay(0U, false, MEL_TUNE, PAIRS(MEL_TUNE));
        s_audio_active = true;
        setStatus("playing melody on track 0");
        gameLog("[buzzer-test] play: buzzerPlay(track 0, %u notes)", (unsigned)PAIRS(MEL_TUNE));
        break;

    case PH_DONEFLAG:
        buzzerPlayWithFlag(0U, false, MEL_TUNE, PAIRS(MEL_TUNE), (bool *)&s_done_flag);
        s_audio_active = true;
        setStatus("playing... awaiting done-flag");
        gameLog("[buzzer-test] done-flag: buzzerPlayWithFlag(track 0)");
        break;

    case PH_PAUSE_RESUME:
        buzzerPlay(0U, false, MEL_LONG, PAIRS(MEL_LONG));
        s_audio_active = true;
        setStatus("playing sustained tone");
        gameLog("[buzzer-test] pause/resume: sustained tone on track 0");
        break;

    case PH_STOP:
        buzzerPlay(0U, false, MEL_LONG, PAIRS(MEL_LONG));
        s_audio_active = true;
        setStatus("playing (stops early)...");
        gameLog("[buzzer-test] stop: sustained tone on track 0");
        break;

    case PH_LOOP:
        buzzerPlay(0U, true, MEL_LOOP, PAIRS(MEL_LOOP));
        s_audio_active = true;
        setStatus("looping motif on track 0");
        gameLog("[buzzer-test] loop: buzzerPlay(track 0, looped)");
        break;

    case PH_ARBITRATION:
        buzzerPlay(0U, true, MEL_LOW, PAIRS(MEL_LOW)); /* low on track 0 */
        s_audio_active = true;
        setStatus("track 0 low tone");
        snprintf(s_extra, sizeof(s_extra), "AUDIBLE: track 0");
        gameLog("[buzzer-test] arbitration: track 0 low playing");
        break;

    case PH_STOPALL:
        buzzerPlay(0U, true, MEL_LOW, PAIRS(MEL_LOW));
        if (s_max_tracks > 1U)
        {
            buzzerPlay(1U, true, MEL_HIGH, PAIRS(MEL_HIGH));
        }
        if (s_max_tracks > 2U)
        {
            buzzerPlay(2U, true, MEL_LOOP, PAIRS(MEL_LOOP));
        }
        s_audio_active = true;
        setStatus("several tracks playing...");
        gameLog("[buzzer-test] stop-all: started tracks 0..%u", (unsigned)(s_max_tracks > 2U ? 2U : s_max_tracks - 1U));
        break;

    case PH_DONE:
        setStatus("ALL TESTS COMPLETE");
        gameLog("[buzzer-test] suite complete");
        break;

    default:
        break;
    }
}

/* Advance a step's internal timeline each frame: fire the timed sub-operations,
 * update the live status line, and log transitions. */
static void phaseTick(uint32_t elapsed)
{
    switch (s_phase)
    {
    case PH_DONEFLAG:
        if (s_done_flag && s_sub == 0U)
        {
            s_sub = 1U;
            s_audio_active = false;
            setStatus("DONE-FLAG SET (track ended)");
            gameLog("[buzzer-test]   done-flag observed at %lu ms", (unsigned long)elapsed);
        }
        break;

    case PH_PAUSE_RESUME:
        if (s_sub == 0U && elapsed >= 1200U)
        {
            buzzerPause(0U);
            s_sub = 1U;
            s_audio_active = false;
            setStatus("PAUSED");
            gameLog("[buzzer-test]   buzzerPause(0)");
        }
        else if (s_sub == 1U && elapsed >= 2600U)
        {
            buzzerResume(0U);
            s_sub = 2U;
            s_audio_active = true;
            setStatus("RESUMED");
            gameLog("[buzzer-test]   buzzerResume(0)");
        }
        break;

    case PH_STOP:
        if (s_sub == 0U && elapsed >= 1500U)
        {
            buzzerStop(0U);
            s_sub = 1U;
            s_audio_active = false;
            setStatus("STOPPED");
            gameLog("[buzzer-test]   buzzerStop(0)");
        }
        break;

    case PH_LOOP:
    {
        const uint32_t len = notesLenMs(MEL_LOOP, PAIRS(MEL_LOOP));
        snprintf(s_extra, sizeof(s_extra), "CYCLES: %lu", (unsigned long)(len ? elapsed / len : 0U));
        break;
    }

    case PH_ARBITRATION:
        if (s_max_tracks < 2U)
        {
            setStatus("needs >= 2 tracks (skipped)");
            break;
        }
        if (s_sub == 0U && elapsed >= 1500U)
        {
            buzzerPlay(1U, true, MEL_HIGH, PAIRS(MEL_HIGH));
            s_sub = 1U;
            setStatus("track 1 high added (wins)");
            snprintf(s_extra, sizeof(s_extra), "AUDIBLE: track 1");
            gameLog("[buzzer-test]   track 1 high -> higher track wins the voice");
        }
        else if (s_sub == 1U && elapsed >= 3200U)
        {
            buzzerStop(1U);
            s_sub = 2U;
            setStatus("track 1 stopped -> 0 returns");
            snprintf(s_extra, sizeof(s_extra), "AUDIBLE: track 0");
            gameLog("[buzzer-test]   buzzerStop(1) -> track 0 sounds again");
        }
        else if (s_sub == 2U && elapsed >= 4700U)
        {
            buzzerStop(0U);
            s_sub = 3U;
            s_audio_active = false;
            setStatus("both stopped");
            snprintf(s_extra, sizeof(s_extra), "AUDIBLE: none");
            gameLog("[buzzer-test]   buzzerStop(0)");
        }
        break;

    case PH_STOPALL:
        if (s_sub == 0U && elapsed >= 1500U)
        {
            buzzerStopAll();
            s_sub = 1U;
            s_audio_active = false;
            setStatus("buzzerStopAll() -> SILENCE");
            gameLog("[buzzer-test]   buzzerStopAll()");
        }
        break;

    default:
        break;
    }
}

static void gotoPhase(Phase p, uint32_t now)
{
    s_phase = p;
    s_phase_start = now;
    phaseEnter(p);
}

/* Render one C string into consecutive UI sprites; glyph pixels come from console
 * flash via fontGet, so no asset pool is needed. Returns the next free index. */
static uint16_t draw_text(uint16_t idx, FontSize size, int16_t x, int16_t y,
                          uint8_t z, const uint16_t *palette, const char *text)
{
    const uint16_t gw = fontGlyphW(size), gh = fontGlyphH(size);
    for (const char *s = text; *s && idx < UI_CAP; s++)
    {
        const uint8_t c = (uint8_t)*s;
        if (c >= 0x20U && c <= 0x7EU)
        {
            const uint8_t *px;
            fontGet(c, size, &px);
            s_ui[idx++] = (Sprite){.x = x, .y = y, .w = gw, .h = gh, .z = z, .flags = 0U, .pixels = px, .palette = palette};
        }
        x = (int16_t)(x + gw + 1U);
    }
    return idx;
}

static void gameInit(void)
{
    s_max_tracks = buzzerGetMaxTracks();
    rendererSetBackground(RGB(10, 12, 26)); /* dark navy backdrop */
    gameLog("buzzer test: %u tracks; steps auto-run, SB1 next, SB2 exit", (unsigned)s_max_tracks);
    gotoPhase(PH_INTRO, getSysTime());
}

static void gameUpdate(void)
{
    InputState in;
    inputGetState(&in);
    if (in.special2.pressed)
    {
        buzzerStopAll(); /* don't leave a tone ringing in the menu */
        gameExit();
    }

    const uint32_t now = getSysTime();
    const uint32_t elapsed = now - s_phase_start;
    const bool skip = in.special1.pressed;

    if (s_phase == PH_DONE)
    {
        if (skip)
        {
            gotoPhase(PH_INTRO, now); /* restart the suite */
        }
        return;
    }

    phaseTick(elapsed);

    const uint32_t dwell = PHASES[s_phase].dwell_ms;
    if (skip || (dwell != 0U && elapsed >= dwell))
    {
        gotoPhase((Phase)(s_phase + 1U), now);
    }
}

static void gameRender(void)
{
    const PhaseInfo *pi = &PHASES[s_phase];
    uint16_t n = 0U;
    char line[24];

    n = draw_text(n, FONT_8x8, 8, 6, 10U, PAL_TITLE, "BUZZER TEST");
    snprintf(line, sizeof(line), "STEP %u/%u", (unsigned)(s_phase + 1U), (unsigned)PH_COUNT);
    n = draw_text(n, FONT_5x5, 250, 8, 10U, PAL_DIM, line);

    n = draw_text(n, FONT_8x8, 8, 28, 10U, PAL_HEAD, pi->title);
    if (s_audio_active && ((getSysTime() / 300U) & 1U))
    {
        n = draw_text(n, FONT_8x8, 262, 28, 10U, PAL_WARN, "SND");
    }

    n = draw_text(n, FONT_5x5, 8, 50, 10U, PAL_TEXT, pi->desc);
    if (pi->desc2 != NULL)
    {
        n = draw_text(n, FONT_5x5, 8, 60, 10U, PAL_TEXT, pi->desc2);
    }

    n = draw_text(n, FONT_8x8, 8, 96, 10U, PAL_STAT, s_status);
    if (s_extra[0] != '\0')
    {
        n = draw_text(n, FONT_8x8, 8, 120, 10U, PAL_HEAD, s_extra);
    }

    n = draw_text(n, FONT_5x5, 8, 226, 10U, PAL_TITLE,
                  (s_phase == PH_DONE) ? "SB1 RESTART   SB2 EXIT" : "SB1 NEXT   SB2 EXIT");

    rendererSubmitLayer(LAYER_UI, s_ui, n);
    rendererRender();
    delay(20U); /* ~50 FPS is plenty for a diagnostic UI (audio is console-timed) */
}

DECLARE_GAME_HEADER(gameInit, gameUpdate, gameRender);
