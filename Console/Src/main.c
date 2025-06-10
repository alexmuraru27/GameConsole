#include <stm32f407xx.h>
#include "sysclock.h"
#include "usart.h"
#include "gpio.h"
#include "dma.h"
#include "ILI9341.h"
#include "adc.h"
#include "timer.h"
#include "joystick.h"
#include "renderer.h"
#include "buzzer.h"
#include "game_console.h"
#include "game_console_api.h"
#include "stddef.h"
#include "sdio.h"
#include "ff.h"
#include "string.h"
#include "test_fatfs.h"
#include "test_buzzer.h"
#include "test_renderer.h"

bool is_debug_fps = false;
#define FPS 50
#define FRAME_PERIOD (1000U / FPS)
uint32_t s_last_frame_time = 0U;

void SystemInit(void)
{
  systemClockConfig();
}

static void syncFrame()
{
  if (getSysTime() - s_last_frame_time < FRAME_PERIOD)
  {
    // Busy-wait until it's time for the next frame
    while ((getSysTime() - s_last_frame_time) < FRAME_PERIOD)
      ;
  }
  if (is_debug_fps)
  {
    debugInt(1000 / (getSysTime() - s_last_frame_time));
    debugString("\r\n");
  }
  s_last_frame_time = getSysTime(); // Keep consistent frame timing
}

static void update()
{
  const uint16_t SPEED = 5U;
  uint8_t x = rendererOamGetXPos(0U);
  uint8_t y = rendererOamGetYPos(0U);
  if (x <= rendererGetWidthPixels() - rendererGetTilePixelSize())
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x >= 0U + SPEED)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y <= rendererGetHeightPixels() - rendererGetTilePixelSize())
  {
    y += ((joystickGetLAnalogY() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogY() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (y >= 0U + SPEED)
  {
    y -= ((joystickGetLAnalogY() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogY() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  rendererOamSetXYPos(0U, x, y);
}

extern uint32_t __game_header_start;
bool is_pressed = false;
static void render()
{
  if (joystickGetSpecialBtn1())
  {
    testBuzzerTrack0();
  }
  if (joystickGetSpecialBtn2())
  {
    testBuzzerTrack1();
  }

  if (joystickGetRBtnUp())
  {
    buzzerPause(0);
  }
  if (joystickGetRBtnDown())
  {
    buzzerResume(0);
  }
  if (joystickGetRBtnLeft())
  {
    buzzerStop(0);
    is_pressed = false;
  }
  if (joystickGetRBtnRight() && !is_pressed)
  {
    is_pressed = true;
    GameHeader *game_header = (GameHeader *)&__game_header_start;

    if (game_header->magic != 0x47414D45U)
    {
      return;
    }

    uint32_t header_size = game_header->header_end - game_header->header_start;
    uint32_t text_size = game_header->text_end - game_header->text_start;
    uint32_t ro_data_size = game_header->ro_data_end - game_header->ro_data_start;
    uint32_t assets_size = game_header->assets_end - game_header->assets_start;
    uint32_t data_size = game_header->data_end - game_header->data_start;
    uint32_t bss_size = game_header->bss_end - game_header->bss_start;
    uint32_t total = header_size + text_size + ro_data_size + assets_size + data_size + bss_size;
    debugString("\r\nheader_size = ");
    debugInt(header_size);
    debugString("\r\ntext_size = ");
    debugInt(text_size);
    debugString("\r\nro_data_size = ");
    debugInt(ro_data_size);
    debugString("\r\ndata_size = ");
    debugInt(data_size);
    debugString("\r\nbss_size = ");
    debugInt(bss_size);
    debugString("\r\nassets_size = ");
    debugInt(assets_size);
    debugString("\r\ntotal = ");
    debugInt(total);

    delay(50);
    delay(4000);
    // __asm volatile("msr msp, %0" ::"r"(game_header->data_end) :);
    // void (*game_entry)(void) = (void (*)(void))game_header->entry_point;
    // game_entry();
  }
  rendererRender();
}

extern ConsoleAPIHeader __game_console_api_start; // linker
static void testApi()
{
  ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
  if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
  {
    api_hdr_ptr->api.debugString("Hello from shared api :D\r\n");
  }
}

int main(void)
{
  gameConsoleInit();
  testApi();
  testRendererInit();
  testFatFs();
  while (1)
  {
    update();
    render();

    syncFrame();
  }
}
