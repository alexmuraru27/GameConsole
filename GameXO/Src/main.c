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
 * The OS owns the loop. It runs the C-runtime bootstrap, then calls gameInit()
 * once, then gameUpdate()/gameRender() every frame (pacing the frame and servicing
 * the console in between). There is no handshake: the loader validated this
 * binary's magic + ABI version before loading it, and the API is reached through
 * SVC traps.
 */

static void gameInit(void)
{
    gameLog("GameXO started");
    bumpLaunchCount();
    gameStateManagerInit();
}

static void gameUpdate(void)
{
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
