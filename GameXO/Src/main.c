#include "game_console_api.h"
#include "game_state_manager.h"
#include "sound.h"
#include "stdio.h"

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
        printf("%lu", 1000 / (api_hdr_ptr->api.getSysTime() - s_last_frame_time));
        printf("\r\n");
    }
    s_last_frame_time = api_hdr_ptr->api.getSysTime();
}

static const uint16_t s_smoke_test_notes[] = {
    261,
    150, // C4
    294,
    150, // D4
    330,
    150, // E4
    349,
    150, // F4
    392,
    150, // G4
    440,
    150, // A4
    494,
    150, // B4
    523,
    300, // C5 (hold longer)
    494,
    150, // B4
    440,
    150, // A4
    392,
    150, // G4
    349,
    150, // F4
    330,
    150, // E4
    294,
    150, // D4
    261,
    400, // C4 (hold at end)
};

int main(void)
{
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == 1U)
    {
        api_hdr_ptr->api.buzzerPlay(
            0U, false,
            s_smoke_test_notes,
            sizeof(s_smoke_test_notes) / sizeof(uint16_t) / 2U);
        // gameStateManagerInit();
        // playSound(ASSET_ID_WELCOME_SOUND);
        while (true)
        {
            // UPDATE
            // gameStateManagerUpdate();

            // RENDER
            // api_hdr_ptr->api.rendererRender();

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

extern void _game_start(void);
DECLARE_GAME_BINARY_HEADER(_game_start);

// Smoke-test song: ascending then descending C major scale.
// {frequency_hz, duration_ms} pairs; frequency 0 = pause.
