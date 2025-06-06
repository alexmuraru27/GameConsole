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
extern uint32_t __game_text_start, __game_text_end;
extern uint32_t __game_ro_data_start, __game_ro_data_end;
extern uint32_t __game_data_init_start, __game_data_init_end;
extern uint32_t __game_data_no_init_start, __game_data_no_init_end;
extern uint32_t __game_code_assets_start, __game_code_assets_end;

__attribute__((section(".game_header")))
const GameHeader game_header = {
    .magic = 0x47414D45, // GAME
    .header_start = (uint32_t)&__game_header_start,
    .header_end = (uint32_t)&__game_header_end,
    .text_start = (uint32_t)&__game_text_start,
    .text_end = (uint32_t)&__game_text_end,
    .ro_data_start = (uint32_t)&__game_ro_data_start,
    .ro_data_end = (uint32_t)&__game_ro_data_end,
    .data_start = (uint32_t)&__game_data_init_start,
    .data_end = (uint32_t)&__game_data_init_end,
    .bss_start = (uint32_t)&__game_data_no_init_start,
    .bss_end = (uint32_t)&__game_data_no_init_end,
    .assets_start = (uint32_t)&__game_code_assets_start,
    .assets_end = (uint32_t)&__game_code_assets_end,
    .entry_point = (uint32_t)&main};

__attribute__((section(".assets"))) const uint8_t pacman_ghost_data[] = {
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
__attribute__((section(".assets"))) const uint8_t pacman_ghost_palette[4U] = {0x20, 0x1c, 0x2c, 0xc};
