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

void SystemInit(void)
{
  systemClockConfig();
}

static void peripheralsInit()
{
  dmaInit();
  gpioInit();
  usartInit();
  timerInit();
  ili9341Init(3U);
  adcInit();
  joystickInit();
}

static void consoleInit()
{
  peripheralsInit();
  rendererInit();
}

const uint16_t system_colors[64U] = {
    RGB2COLOR(98, 98, 98),    // 0
    RGB2COLOR(0, 46, 152),    // 1
    RGB2COLOR(12, 17, 194),   // 2
    RGB2COLOR(59, 0, 194),    // 3
    RGB2COLOR(101, 0, 152),   // 4
    RGB2COLOR(125, 0, 78),    // 5
    RGB2COLOR(125, 0, 0),     // 6
    RGB2COLOR(101, 25, 0),    // 7
    RGB2COLOR(59, 54, 0),     // 8
    RGB2COLOR(12, 79, 0),     // 9
    RGB2COLOR(0, 91, 0),      // 10
    RGB2COLOR(0, 89, 0),      // 11
    RGB2COLOR(0, 73, 78),     // 12
    RGB2COLOR(0, 0, 0),       // 13
    RGB2COLOR(0, 0, 0),       // 14
    RGB2COLOR(0, 0, 0),       // 15
    RGB2COLOR(171, 171, 171), // 16
    RGB2COLOR(0, 100, 243),   // 17
    RGB2COLOR(53, 60, 255),   // 18
    RGB2COLOR(118, 27, 255),  // 19
    RGB2COLOR(174, 10, 243),  // 20
    RGB2COLOR(206, 13, 143),  // 21
    RGB2COLOR(206, 35, 28),   // 22
    RGB2COLOR(174, 71, 0),    // 23
    RGB2COLOR(118, 111, 0),   // 24
    RGB2COLOR(53, 114, 0),    // 25
    RGB2COLOR(0, 161, 0),     // 26
    RGB2COLOR(0, 158, 28),    // 27
    RGB2COLOR(0, 136, 143),   // 28
    RGB2COLOR(0, 0, 0),       // 29
    RGB2COLOR(0, 0, 0),       // 30
    RGB2COLOR(0, 0, 0),       // 31
    RGB2COLOR(255, 255, 255), // 32
    RGB2COLOR(76, 181, 255),  // 33
    RGB2COLOR(133, 140, 255), // 34
    RGB2COLOR(200, 107, 255), // 35
    RGB2COLOR(255, 89, 255),  // 36
    RGB2COLOR(255, 92, 225),  // 37
    RGB2COLOR(255, 115, 107), // 38
    RGB2COLOR(255, 152, 5),   // 39
    RGB2COLOR(200, 192, 0),   // 40
    RGB2COLOR(133, 226, 0),   // 41
    RGB2COLOR(76, 244, 5),    // 42
    RGB2COLOR(43, 241, 107),  // 43
    RGB2COLOR(43, 218, 225),  // 44
    RGB2COLOR(78, 78, 78),    // 45
    RGB2COLOR(0, 0, 0),       // 46
    RGB2COLOR(0, 0, 0),       // 47
    RGB2COLOR(255, 255, 255), // 48
    RGB2COLOR(184, 255, 255), // 49
    RGB2COLOR(206, 209, 255), // 50
    RGB2COLOR(232, 196, 255), // 51
    RGB2COLOR(255, 189, 255), // 52
    RGB2COLOR(255, 190, 243), // 53
    RGB2COLOR(255, 199, 196), // 54
    RGB2COLOR(255, 214, 156), // 55
    RGB2COLOR(232, 230, 132), // 56
    RGB2COLOR(206, 243, 132), // 57
    RGB2COLOR(184, 250, 156), // 58
    RGB2COLOR(171, 249, 196), // 59
    RGB2COLOR(171, 240, 243), // 60
    RGB2COLOR(184, 184, 184), // 61
    RGB2COLOR(0, 0, 0),       // 62
    RGB2COLOR(0, 0, 0)        // 63
};

// TODO Remove statics :)
static uint16_t x = 0U;
static uint16_t y = 0U;
static uint8_t framecount = 0U;
static uint32_t lastFrameTime = 0U;
const uint16_t TILE_SIZE = 32U;
const uint16_t SPEED = 5U;

static void update()
{
  if (x < 320U - TILE_SIZE - SPEED)
  {
    x += ((joystickGetLAnalogX() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogX() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (x > 0U)
  {
    x -= ((joystickGetLAnalogX() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogX() == JoystickAnalogValueLowAxis)) * SPEED;
  }

  if (y < 240U - TILE_SIZE - SPEED)
  {
    y += ((joystickGetLAnalogY() == JoystickAnalogValueHighAxis) || (joystickGetRAnalogY() == JoystickAnalogValueHighAxis)) * SPEED;
  }
  if (y > 0U)
  {
    y -= ((joystickGetLAnalogY() == JoystickAnalogValueLowAxis) || (joystickGetRAnalogY() == JoystickAnalogValueLowAxis)) * SPEED;
  }
}

static void render()
{
  if (framecount % 5 == 0)
  {
    ili9341FillScreen(ILI9341_GREENYELLOW);
  }
  ili9341FillRectangle(x, y, TILE_SIZE, TILE_SIZE, ILI9341_RED);
  rendererRender();
}

int main(void)
{
  consoleInit();
  rendererSetPaletteSystem(system_colors, 64U);

  while (1)
  {
    update();
    render();

    // 33ms frame time
    if (getSysTime() - lastFrameTime < 33U)
    {
      // Busy-wait until it's time for the next frame
      while ((getSysTime() - lastFrameTime) < 33U)
        ;
    }
    lastFrameTime = getSysTime(); // Keep consistent frame timing
    framecount++;
  }
}
