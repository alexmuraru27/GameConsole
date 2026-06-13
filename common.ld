TOTAL_RAM_SIZE = 128K;
CONSOLE_RAM_SIZE = 62K;
GAME_RAM_SIZE = 64K;
SHARED_RAM_SIZE = 2K;

RAM_START = 0x20000000;
RAM_START_CONSOLE = RAM_START + SHARED_RAM_SIZE;
RAM_START_GAME = RAM_START_CONSOLE + CONSOLE_RAM_SIZE;
RAM_END = 0x20020000;

ASSERT(SHARED_RAM_SIZE + CONSOLE_RAM_SIZE + GAME_RAM_SIZE == TOTAL_RAM_SIZE,
       "SRAM region sizes must sum to 128K");
MEMORY
{
    SHARED_RAM      (rw)    : ORIGIN = RAM_START, LENGTH = SHARED_RAM_SIZE
    CONSOLE_RAM     (rw)    : ORIGIN = RAM_START_CONSOLE, LENGTH = CONSOLE_RAM_SIZE
    /* Game code + data + stack — regular SRAM (I-bus + D-bus, executable). */
    GAME_RAM        (rwx)   : ORIGIN = RAM_START_GAME, LENGTH = GAME_RAM_SIZE
    /* Asset arena in CCM — D-bus only, not executable. */
    GAME_RAM_ASSET  (rw)    : ORIGIN = 0x10000000, LENGTH = 64K
    CONSOLE_FLASH   (rx)    : ORIGIN = 0x8000000, LENGTH = 512K
}

SECTIONS
{
    /* Shared RAM section - for common data or API access */
    .game_console_api :
    {
        __game_console_api_start = .;
        KEEP(*(.game_console_api))
        __game_console_api_end = .;
    } > SHARED_RAM


    /* Game binary header — placed at the start of game RAM */
    .game_header :
    {
        __game_header_start = .;
        KEEP(*(.game_header))
        __game_header_end = .;
    } > GAME_RAM
}
