#include "game_console_api.h"
#include "game_state_manager.h"


/* Persisted across runs through the console settings store, keyed by the .bin
 * name. Bump the version if this struct ever changes. */
#define GAMEXO_SETTINGS_VERSION 1U
typedef struct
{
    uint32_t launch_count;
} __attribute__((packed)) GameXoSettings;

/* Load the launch counter, bump it, and persist it — proves the settings round
 * trip end to end (slot auto-created by the loader on first launch). */
static void bumpLaunchCount(void)
{
    GameXoSettings settings = {0};
    uint16_t size = (uint16_t)sizeof(settings);

    const uint8_t read_status = settingsRead(GAMEXO_SETTINGS_VERSION, (uint8_t *)&settings, &size);
    if (read_status == SETTINGS_STORAGE_STATUS_OK)
    {
        gameLog("settings: launch_count=%lu", (unsigned long)settings.launch_count);
    }
    else
    {
        settings.launch_count = 0U;
        gameLog("settings: none yet (%u), starting fresh", (unsigned)read_status);
    }

    settings.launch_count++;
    const uint8_t write_status = settingsWrite(GAMEXO_SETTINGS_VERSION, (const uint8_t *)&settings, sizeof(settings));
    gameLog("settings: launch #%lu saved (%u)", (unsigned long)settings.launch_count, (unsigned)write_status);
}

/*
 * The OS owns the loop but runs games uncapped: it runs the C-runtime bootstrap,
 * then calls gameInit() once, then gameUpdate()/gameRender() back to back every
 * frame. A game that wants a fixed frame rate paces itself — GameXO holds a steady
 * ~60 FPS by sleeping out the rest of each frame budget with delay(). There is no
 * handshake: the loader validated this binary's magic + ABI version before loading
 * it, and the API is reached through SVC traps.
 */
#define TARGET_FRAME_MS (1000U / 60U) /* ~60 FPS frame budget */
static uint32_t s_frame_deadline_ms;

/* Hold the target frame rate: sleep out whatever time is left until the current
 * frame's deadline, then arm the next one. A frame that overruns its budget just
 * runs back to back (no catch-up), so a slow frame can't snowball. */
static void paceFrame(void)
{
    const int32_t remaining = (int32_t)(s_frame_deadline_ms - getSysTime());
    if (remaining > 0)
    {
        delay((uint32_t)remaining);
    }
    s_frame_deadline_ms = getSysTime() + TARGET_FRAME_MS;
}

static void gameInit(void)
{
    gameLog("GameXO started");
    bumpLaunchCount();
    gameStateManagerInit();
    s_frame_deadline_ms = getSysTime() + TARGET_FRAME_MS;
}

static void gameUpdate(void)
{
    paceFrame(); /* OS no longer caps the loop; GameXO self-paces to ~60 FPS */
    gameStateManagerUpdate();
    if (joystickGetSpecialBtn2())
    {
        buzzerStopAll(); /* don't leave the buzzer reading game RAM after we return */
        gameExit();      /* Special Button 2 returns to the console OS (does not return) */
    }
}

static void gameRender(void)
{
    gameStateManagerRender();
}

/* Emit the binary header: magic + ABI version + the runtime stubs are filled in
 * for us; we just name our three callbacks. */
DECLARE_GAME_HEADER(gameInit, gameUpdate, gameRender);
