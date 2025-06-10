#include "sysclock.h"
#include <stm32f407xx.h>
#include "usart.h"
#include "gpio.h"
#include "dma.h"
#include "ILI9341.h"
#include "adc.h"
#include "timer.h"
#include "joystick.h"
#include "renderer.h"
#include "buzzer.h"
#include "pacman_ghost.h"
#include "bricks1.h"
#include "bricks2.h"
#include "bricks3.h"
#include "bricks4.h"
#include "bricks5.h"
#include "game_console.h"
#include "game_console_api.h"
#include "stddef.h"
#include "sdio.h"
#include "ff.h"
#include "string.h"
#include "test_fatfs.h"
#include "test_buzzer.h"

bool is_debug_fps = false;
#define FPS 50
#define FRAME_PERIOD (1000U / FPS)
uint32_t s_last_frame_time = 0U;
static FATFS s_fatfs;

void SystemInit(void)
{
  systemClockConfig();
}

static void consoleInit()
{
  dmaInit();
  gpioInit();
  usartInit();
  timerInit();
  ili9341Init(3U, rendererGetWidthPixels(), rendererGetHeightPixels());
  adcInit();
  joystickInit();
  buzzerInit();
  rendererInit();
  f_mount(&s_fatfs, "0:", 1U);
  gameConsoleInit();
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
    __asm volatile("msr msp, %0" ::"r"(game_header->data_end) :);
    void (*game_entry)(void) = (void (*)(void))game_header->entry_point;
    game_entry();
  }
  rendererRender();
}

static void screenInit()
{
  rendererPatternTableSetTile(1U, pacman_ghost_data, sizeof(pacman_ghost_data));
  rendererFramePaletteSetSpriteMultiple(0U, pacman_ghost_palette[1U], pacman_ghost_palette[2U], pacman_ghost_palette[3U]);
  rendererOamSetTileIdx(0U, 1U);
  rendererOamSetPaletteIdx(0U, 0U);
  rendererOamSetFlipH(0U, true);
  rendererOamSetPriorityLow(0U, true);

  // backgrounds
  rendererPatternTableSetTile(2U, bricks1_data, sizeof(bricks1_data));
  rendererPatternTableSetTile(3U, bricks2_data, sizeof(bricks2_data));
  rendererPatternTableSetTile(4U, bricks3_data, sizeof(bricks3_data));
  rendererPatternTableSetTile(5U, bricks4_data, sizeof(bricks4_data));
  rendererPatternTableSetTile(6U, bricks5_data, sizeof(bricks5_data));

  // background frame palette
  const uint8_t brick_pallete_idx = 1U;
  rendererFramePaletteSetBackgroundMultiple(brick_pallete_idx, bricks1_palette[1U], bricks1_palette[2U], bricks1_palette[3U]);

  // tile [2,0] flipped in both directions
  rendererAttributeTableSetFlipH(2U, 0U, true);
  rendererAttributeTableSetFlipV(2U, 0U, true);

  // background set nametable
  rendererNameTableSetTile(5U, 5U, 2U);
  rendererNameTableSetTile(5U, 5U + 1U, 3U);
  rendererNameTableSetTile(5U, 5U + 2U, 4U);
  rendererNameTableSetTile(5U, 5U + 3U, 5U);
  rendererNameTableSetTile(5U, 5U + 4U, 6U);

  rendererAttributeTableSetPalette(5U, 5U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 1U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 2U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 3U, brick_pallete_idx);
  rendererAttributeTableSetPalette(5U, 5U + 4U, brick_pallete_idx);

  for (uint8_t i = 0; i < rendererGetHeightTiles(); i++)
  {
    rendererNameTableSetTile(0U, i, 4U);
    rendererNameTableSetTile(rendererGetWidthTiles() - 1, i, 4U);

    rendererAttributeTableSetPalette(0U, i, brick_pallete_idx);
    rendererAttributeTableSetPalette(rendererGetWidthTiles() - 1, i, brick_pallete_idx);

    rendererAttributeTableSetPriorityHigh(0U, i, true);
    rendererAttributeTableSetPriorityHigh(rendererGetWidthTiles() - 1, i, true);
  }
  for (uint8_t i = 0; i < rendererGetWidthTiles(); i++)
  {
    rendererNameTableSetTile(i, 0U, 6U);
    rendererNameTableSetTile(i, rendererGetHeightTiles() - 1, 6U);

    rendererAttributeTableSetPalette(i, 0U, brick_pallete_idx);
    rendererAttributeTableSetPalette(i, rendererGetHeightTiles() - 1, brick_pallete_idx);

    rendererAttributeTableSetPriorityHigh(i, 0U, false);
    rendererAttributeTableSetPriorityHigh(i, rendererGetHeightTiles() - 1, true);
  }
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
  consoleInit();
  screenInit();
  testApi();

  testFatFs();
  while (1)
  {
    update();
    render();

    syncFrame();
  }
}
