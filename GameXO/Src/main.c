#include "game_console_api.h"

extern ConsoleAPIHeader __game_console_api_start; // linker
static void testApi()
{
    ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
    {
        api_hdr_ptr->api.debugString("Hello from game shared api :D\r\n");
    }
}

int main(void)
{

    ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
    while (true)
    {
        api_hdr_ptr->api.debugString("In Loop\r\n");
        asm("nop");
        testApi();
    }
}

extern uint32_t __game_header_start, __game_header_end;
extern uint32_t __text_start, __text_end;
extern uint32_t __data_start, __data_end;
extern uint32_t __assets_start, __assets_end;

__attribute__((section(".game_header")))
const GameHeader game_header = {
    .magic = 0x47414D45, // GAME
    .version = 1U,
    .game_header_start = (uint32_t)&__game_header_start,
    .game_header_end = (uint32_t)&__game_header_end,
    .text_start = (uint32_t)&__text_start,
    .text_end = (uint32_t)&__text_end,
    .data_start = (uint32_t)&__data_start,
    .data_end = (uint32_t)&__data_end,
    .assets_start = (uint32_t)&__assets_start,
    .assets_end = (uint32_t)&__assets_end,
    .entry_point = (uint32_t)&main};
