#include "game_console_api.h"
#include "game_state_manager.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);
#define API (api_hdr_ptr->api)

#define FPS 30U
#define FRAME_PERIOD (1000U / FPS)

/* Persisted across runs through the console settings store, keyed by the .bin
 * name. Bump the version if this struct ever changes. */
#define GAMEXO_SETTINGS_VERSION 1U
typedef struct
{
    uint32_t launch_count;
} __attribute__((packed)) GameXoSettings;

static uint32_t s_last_frame_time;

/* Load the launch counter, bump it, and persist it — proves the settings round
 * trip end to end (slot auto-created by the loader on first launch). */
static void bumpLaunchCount(void)
{
    GameXoSettings settings = {0};
    uint16_t size = (uint16_t)sizeof(settings);

    const uint8_t read_status = API.settingsRead(GAMEXO_SETTINGS_VERSION, (uint8_t *)&settings, &size);
    if (read_status == SETTINGS_STORAGE_STATUS_OK)
    {
        API.log("settings: launch_count=%lu", (unsigned long)settings.launch_count);
    }
    else
    {
        settings.launch_count = 0U;
        API.log("settings: none yet (%u), starting fresh", (unsigned)read_status);
    }

    settings.launch_count++;
    const uint8_t write_status = API.settingsWrite(GAMEXO_SETTINGS_VERSION, (const uint8_t *)&settings, sizeof(settings));
    API.log("settings: launch #%lu saved (%u)", (unsigned long)settings.launch_count, (unsigned)write_status);
}

static void syncFrame(void)
{
    while ((API.getSysTime() - s_last_frame_time) < FRAME_PERIOD)
    {
        /* busy-wait to the next frame boundary */
    }
    s_last_frame_time = API.getSysTime();
}

int main(void)
{
    if (api_hdr_ptr->magic != API_MAGIC)
    {
        return 0; /* console API not present — refuse to run */
    }
    API.log("GameXO started (API v%lu)", (unsigned long)api_hdr_ptr->version);

    bumpLaunchCount();

    gameStateManagerInit();
    s_last_frame_time = API.getSysTime();

    while (true)
    {
        gameStateManagerUpdate();
        if (API.joystickGetSpecialBtn2())
        {
            break; /* Special Button 2 returns to the console OS */
        }
        syncFrame();
    }

    API.buzzerStopAll(); /* don't leave the buzzer reading game RAM after we return */
    return 0;
}

extern void _game_start(void);
DECLARE_GAME_BINARY_HEADER(_game_start, 1);
