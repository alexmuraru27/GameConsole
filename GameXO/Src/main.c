#include "game_console_api.h"
#include "assets.h"
#include "level_manager.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);
#define FPS 50
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
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
    {
        api_hdr_ptr->api.debugString("Hello from GameXO :D\r\n");

        levelManagerInit();

        while (true)
        {
            // update();
            api_hdr_ptr->api.rendererRender();

            syncFrame();

            if (api_hdr_ptr->api.joystickGetSpecialBtn2())
            {
                break;
            }
        }
    }
}

DECLARE_GAME_BINARY_HEADER(main);