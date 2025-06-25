#include "game_console_api.h"
#include "assets.h"
#include "game_state_manager.h"
#include "sound.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);
#define FPS 30
#define FRAME_PERIOD (1000U / FPS)
uint32_t s_last_frame_time = 0U;
bool is_debug_fps = false;

static void syncFrame()
{
    if (api_hdr_ptr->api.getSysTime() - s_last_frame_time < FRAME_PERIOD)
    {
        while ((api_hdr_ptr->api.getSysTime() - s_last_frame_time) < FRAME_PERIOD)
            ;
    }
    if (is_debug_fps)
    {
        api_hdr_ptr->api.debugInt(1000 / (api_hdr_ptr->api.getSysTime() - s_last_frame_time));
        api_hdr_ptr->api.debugString("\r\n");
    }
    s_last_frame_time = api_hdr_ptr->api.getSysTime();
}

int main(void)
{
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == 1U)
    {
        gameStateManagerInit();
        playSound(ASSET_ID_WELCOME_SOUND, ASSET_ID_WELCOME_SOUND_DURATION);
        while (true)
        {
            // UPDATE
            gameStateManagerUpdate();

            // RENDER
            api_hdr_ptr->api.rendererRender();

            // SYNC frame -> 50fps
            syncFrame();
            if (api_hdr_ptr->api.joystickGetSpecialBtn2())
            {
                // Special Button 2 returns to console OS
                break;
            }
        }
    }
}

DECLARE_GAME_BINARY_HEADER(main);