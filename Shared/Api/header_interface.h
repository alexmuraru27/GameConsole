#ifndef __GAME_HEADER_API_H
#define __GAME_HEADER_API_H

typedef struct
{
    uint32_t magic; // Just to identify it's a valid game file
    uint32_t header_start;
    uint32_t header_end;
    uint32_t text_start;
    uint32_t text_end;
    uint32_t ro_data_start;
    uint32_t ro_data_end;
    uint32_t data_start;
    uint32_t data_end;
    uint32_t bss_start;
    uint32_t bss_end;
    uint32_t stack_top;
    uint32_t entry_point;
    uint32_t has_settings; // non-zero if the game persists settings (loader reserves a save slot)
} GameBinaryHeader;

// MAGIC = GAME in ASCII. `has_settings` is non-zero when the game uses the
// settings API; the loader then binds a save slot keyed by the game's .bin name.
#define DECLARE_GAME_BINARY_HEADER(entry_func, game_has_settings)       \
    extern uint32_t __game_header_start, __game_header_end;             \
    extern uint32_t __game_text_start, __game_text_end;                 \
    extern uint32_t __game_ro_data_start, __game_ro_data_end;           \
    extern uint32_t __game_data_init_start, __game_data_init_end;       \
    extern uint32_t __game_data_no_init_start, __game_data_no_init_end; \
    extern uint32_t _estack;                                            \
    __attribute__((section(".game_header")))                            \
    const GameBinaryHeader game_header_variable_47414D45 = {            \
        .magic = 0x47414D45,                                            \
        .header_start = (uint32_t)&__game_header_start,                 \
        .header_end = (uint32_t)&__game_header_end,                     \
        .text_start = (uint32_t)&__game_text_start,                     \
        .text_end = (uint32_t)&__game_text_end,                         \
        .ro_data_start = (uint32_t)&__game_ro_data_start,               \
        .ro_data_end = (uint32_t)&__game_ro_data_end,                   \
        .data_start = (uint32_t)&__game_data_init_start,                \
        .data_end = (uint32_t)&__game_data_init_end,                    \
        .bss_start = (uint32_t)&__game_data_no_init_start,              \
        .bss_end = (uint32_t)&__game_data_no_init_end,                  \
        .stack_top = (uint32_t)&_estack,                                \
        .entry_point = (uint32_t)&entry_func,                           \
        .has_settings = (game_has_settings)}

#endif