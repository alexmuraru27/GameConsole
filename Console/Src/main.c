#include <stm32f407xx.h>
#include "sysclock.h"
#include "usart.h"
#include "joystick.h"
#include "renderer.h"
#include "buzzer.h"
#include "game_console.h"
#include "game_console_api.h"
#include "stddef.h"
#include "string.h"
#include "test_buzzer.h"
#include "test_renderer.h"
#include "loader.h"
#include "game_loader.h"
#include "asset_loader.h"

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
  }
  rendererRender();
}

void loaderTest(void)
{
  debugString("\r\nLoader number of binary files found: ");
  debugInt(loaderGetBinaryFilesNumberInDirectory());
  debugString("\r\n");

  gameLoaderLoadGame(0U);

  // uint8_t assetLoaderGetAssetSize(uint32_t asset_id, uint8_t *buffer_size);
  // uint8_t assetLoaderGetAssetData(uint32_t asset_id, uint8_t *buffer);
  // uint8_t assetLoaderGetAssetHeader(AssetHeader * asset_header);
  // #define ASSET_NUMBER_SEQ_DATA 15U
  // #define ASSET_NUMBER_SMALL_SEQ_DATA 16U

  // #define ASSET_TYPE_TEST_DATA_1 1U
  // #define ASSET_TYPE_TEST_DATA_2 2U
  AssetHeader assetHeader;
  assetLoaderGetAssetHeader(&assetHeader);
  debugString(assetHeader.magic);

  uint32_t asset_1_size = 0U;
  assetLoaderGetAssetSize(15U, &asset_1_size);
  debugString("\r\nAsset size:");
  debugInt(asset_1_size);
  debugString("\r\n");

  uint32_t asset_2_size = 0U;
  assetLoaderGetAssetSize(16U, &asset_2_size);
  debugString("Asset size:");
  debugInt(asset_2_size);
  debugString("\r\n");

  uint8_t asset_1_data[asset_1_size];
  assetLoaderGetAssetData(15U, asset_1_data);

  for (uint8_t i = 0U; i < asset_1_size; i++)
  {
    debugInt(asset_1_data[i]);
    debugString(" ");
  }

  uint8_t asset_2_data[asset_2_size];
  assetLoaderGetAssetData(16U, asset_2_data);

  for (uint8_t i = 0U; i < asset_2_size; i++)
  {
    debugHex(asset_2_data[i]);
    debugString(" ");
  }
}

int main(void)
{
  gameConsoleInit();
  testRendererInit();
  // testFatFs();
  // testApi();
  loaderTest();
  while (1)
  {
    update();
    render();

    syncFrame();
  }
}
