#include "game_console_api.h"

int main(void)
{
}

extern uint32_t __text_start, __text_size;
extern uint32_t __data_start, __data_size;
extern uint32_t __assets_start, __assets_size;

__attribute__((section(".game_header")))
const GameHeader game_header = {
    .magic = 0x47414D45, // GAME in ascii
    .text_start = (uint32_t)&__text_start,
    .text_size = (uint32_t)&__text_size,
    .data_start = (uint32_t)&__data_start,
    .data_size = (uint32_t)&__data_size,
    .assets_start = (uint32_t)&__assets_start,
    .assets_size = (uint32_t)&__assets_size,
    .entry_point = (uint32_t)&main};
