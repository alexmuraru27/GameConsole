TOTAL_RAM_SIZE = 128K; 
CONSOLE_RAM_SIZE = 62K;
GAME_RAM_SIZE = 64K ;
SHARED_RAM_SIZE = 2K;

RAM_START = 0x20000000;
RAM_START_GAME = RAM_START + CONSOLE_RAM_SIZE + SHARED_RAM_SIZE;
RAM_END = 0x20020000;

ASSERT(GAME_RAM_SIZE == 64K, "Game should fit in 64K");
ASSERT(SHARED_RAM_SIZE + GAME_RAM_SIZE + CONSOLE_RAM_SIZE == TOTAL_RAM_SIZE, "RAM region sizes must sum to 128K");
MEMORY
{
    SHARED_RAM      (rw)    : ORIGIN = RAM_START , LENGTH = SHARED_RAM_SIZE
    CONSOLE_RAM     (rw)    : ORIGIN = RAM_START + SHARED_RAM_SIZE, LENGTH = CONSOLE_RAM_SIZE
    GAME_RAM_ASSET (rw)  : ORIGIN = RAM_START_GAME, LENGTH = GAME_RAM_SIZE
    GAME_RAM_CCM  (rwx)   : ORIGIN = 0x10000000, LENGTH = 64K
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
    } > GAME_RAM_CCM
}
