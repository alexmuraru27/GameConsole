TOTAL_RAM_SIZE = 128K; 
CONSOLE_RAM_SIZE = 32K;
GAME_RAM_EXEC_SIZE = 28K;
GAME_RAM_DATA_SIZE = 24K;
GAME_RAM_ASSETS_SIZE = 42K;
GAME_RAM_SIZE = GAME_RAM_EXEC_SIZE + GAME_RAM_DATA_SIZE +GAME_RAM_ASSETS_SIZE;
SHARED_RAM_SIZE = 2K;

RAM_START = 0x20000000;
RAM_START_GAME = 0x20000000 + CONSOLE_RAM_SIZE;
RAM_END = 0x20020000;

ASSERT(GAME_RAM_SIZE == 94K, "Game should fit in 96K");
ASSERT(SHARED_RAM_SIZE + GAME_RAM_SIZE + CONSOLE_RAM_SIZE == TOTAL_RAM_SIZE, "RAM region sizes must sum to 128K");
MEMORY
{
    CONSOLE_RAM     (rwx)   : ORIGIN = RAM_START, LENGTH = CONSOLE_RAM_SIZE
    GAME_RAM_EXEC   (rx)    : ORIGIN = RAM_START_GAME, LENGTH = GAME_RAM_EXEC_SIZE
    GAME_RAM_DATA   (rw)    : ORIGIN = RAM_START_GAME + GAME_RAM_EXEC_SIZE, LENGTH = GAME_RAM_DATA_SIZE
    GAME_RAM_ASSETS (rw)     : ORIGIN = RAM_START_GAME + GAME_RAM_EXEC_SIZE + GAME_RAM_DATA_SIZE, LENGTH = GAME_RAM_ASSETS_SIZE
    SHARED_RAM      (rw)    : ORIGIN = RAM_END - SHARED_RAM_SIZE, LENGTH = SHARED_RAM_SIZE
    CONSOLE_CCMRAM  (rw)    : ORIGIN = 0x10000000, LENGTH = 64K
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


    .game_header :
    {
        __game_header_start = .;
        KEEP(*(.game_header))
        __game_header_end = .;
    } > GAME_RAM_EXEC
}
