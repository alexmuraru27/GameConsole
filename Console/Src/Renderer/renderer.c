#include "renderer.h"
#include "string.h"
#include "ILI9341.h"

#define RENDERER_WIDTH 320U
#define RENDERER_HEIGHT 240U
#define RENDERER_SCANLINES 16U       // number of scanlines computed
#define RENDERER_SCANLINE_BUFFERS 2U // number of scanline buffers
#define RENDERER_SCANLINE_BUFFER_SIZE RENDERER_SCANLINES *RENDERER_WIDTH

#define RENDERER_SYSTEM_PALETTE_SIZE 64U
// System colors from 0x00-0x3F = 64 colors. RGB565
const uint16_t s_system_palette[RENDERER_SYSTEM_PALETTE_SIZE] = {
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

static uint16_t s_scanlines_buffers[RENDERER_SCANLINE_BUFFERS][RENDERER_SCANLINE_BUFFER_SIZE];

void rendererInit(void)
{
    memset(&s_scanlines_buffers[0U], 0U, RENDERER_SCANLINE_BUFFER_SIZE * sizeof(uint16_t));
    memset(&s_scanlines_buffers[1U], 0U, RENDERER_SCANLINE_BUFFER_SIZE * sizeof(uint16_t));
}

void rendererRender(void)
{
}
