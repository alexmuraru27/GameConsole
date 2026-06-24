/* SRAM split. The boundaries are chosen so the game's RAM is an MPU-friendly
 * region: GAME_RAM is a power-of-two size (32K) aligned to its size (0x20018000),
 * so a single ARMv7-M MPU region can confine an unprivileged game to it with no
 * sub-region tricks. CONSOLE_RAM takes the remainder (96K). There is no shared
 * RAM: a game reaches the console only through SVC syscalls, never a shared
 * struct, so the old 2K window was reclaimed. */
TOTAL_RAM_SIZE = 128K;
CONSOLE_RAM_SIZE = 96K;
GAME_RAM_SIZE = 32K;

RAM_START = 0x20000000;
RAM_START_CONSOLE = RAM_START;
RAM_START_GAME = RAM_START_CONSOLE + CONSOLE_RAM_SIZE;   /* = 0x20018000, 32K-aligned */
RAM_END = 0x20020000;

ASSERT(CONSOLE_RAM_SIZE + GAME_RAM_SIZE == TOTAL_RAM_SIZE,
       "SRAM region sizes must sum to 128K");
/* GAME_RAM must be power-of-two sized and size-aligned for a clean MPU region. */
ASSERT(RAM_START_GAME == 0x20018000, "GAME_RAM must start at 0x20018000 (32K-aligned) for the MPU");

MEMORY
{
    CONSOLE_RAM     (rw)    : ORIGIN = RAM_START_CONSOLE, LENGTH = CONSOLE_RAM_SIZE
    /* Game code + data + stack — regular SRAM (I-bus + D-bus, executable). */
    GAME_RAM        (rwx)   : ORIGIN = RAM_START_GAME, LENGTH = GAME_RAM_SIZE
    /* Asset arena in CCM — D-bus only, not executable. */
    GAME_RAM_ASSET  (rw)    : ORIGIN = 0x10000000, LENGTH = 64K
    /* The console OS (application) runs above the bootloader: sector 0 (16K at
     * 0x08000000) is the self-flash bootloader, sectors 1-5 (240K at 0x08004000)
     * are the app, sectors 6-7 (256K at 0x08040000) are the OS-update staging
     * scratch. The app must fit its 240K window (a link overflow flags growth past
     * the staging region). Keep this in sync with Console/Inc/Flasher/flash_map.h. */
    CONSOLE_FLASH   (rx)    : ORIGIN = 0x8004000, LENGTH = 240K
}

/* Region bounds exported as absolute symbols so the console kernel (MPU setup,
 * syscall pointer validation) can reference them without hard-coding addresses.
 * Harmless in the game link (unreferenced). */
__game_ram_start       = ORIGIN(GAME_RAM);
__game_ram_size        = LENGTH(GAME_RAM);
__game_ram_asset_start = ORIGIN(GAME_RAM_ASSET);
__game_ram_asset_size  = LENGTH(GAME_RAM_ASSET);
__console_flash_start  = ORIGIN(CONSOLE_FLASH);
__console_flash_size   = LENGTH(CONSOLE_FLASH);

SECTIONS
{
    /* Game binary header — placed at the start of game RAM (offset 0 of the .bin) */
    .game_header :
    {
        __game_header_start = .;
        KEEP(*(.game_header))
        __game_header_end = .;
    } > GAME_RAM
}
