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

// testing purposes
static volatile uint8_t s_data_array[5U] = {1U, 2U, 3U, 4U, 5U};
static volatile uint8_t s_bss[7U];
__attribute__((section(".assets.data"))) volatile const uint8_t pacman_ghost_data[] = {
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
__attribute__((section(".assets.data"))) volatile const uint8_t pacman_ghost_palette[4U] = {0x20, 0x1c, 0x2c, 0xc};

int main(void)
{
    ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
    for (uint8_t i = 0U; i < 16U; i++)
    {
        api_hdr_ptr->api.debugInt(pacman_ghost_data[i]);
    }
    api_hdr_ptr->api.debugHex(pacman_ghost_palette[0U]);
    api_hdr_ptr->api.debugHex(pacman_ghost_palette[1U]);
    api_hdr_ptr->api.debugHex(pacman_ghost_palette[2U]);
    api_hdr_ptr->api.debugHex(pacman_ghost_palette[3U]);
    while (true)
    {
        api_hdr_ptr->api.debugString("Data:");
        api_hdr_ptr->api.debugInt(s_data_array[0]);
        api_hdr_ptr->api.debugInt(s_data_array[1]);
        api_hdr_ptr->api.debugInt(s_data_array[2]);
        api_hdr_ptr->api.debugInt(s_data_array[3]);
        api_hdr_ptr->api.debugInt(s_data_array[4]);
        api_hdr_ptr->api.debugString("Bss:");
        api_hdr_ptr->api.debugInt(s_bss[0]);
        api_hdr_ptr->api.debugInt(s_bss[1]);
        api_hdr_ptr->api.debugInt(s_bss[2]);
        api_hdr_ptr->api.debugInt(s_bss[3]);
        api_hdr_ptr->api.debugInt(s_bss[4]);
        api_hdr_ptr->api.debugInt(s_bss[5]);
        api_hdr_ptr->api.debugInt(s_bss[6]);
        api_hdr_ptr->api.debugString("In Loop\r\n");
        asm("nop");
        testApi();
        api_hdr_ptr->api.delay(500);
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
