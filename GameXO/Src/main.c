#include "game_console_api.h"
#include "game_state_manager.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);
#define API (api_hdr_ptr->api)

#define FPS 30U
#define FRAME_PERIOD (1000U / FPS)

static uint32_t s_last_frame_time;

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
DECLARE_GAME_BINARY_HEADER(_game_start);
