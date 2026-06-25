/*
 * Bootloader linker script. The bootloader owns sector 0 (16 KB at 0x08000000) —
 * the reset vector and the power-fail-safe OS apply logic. It is flashed once over
 * SWD and is never touched by the self-flasher. It runs before the application and
 * uses the full SRAM (the app is not running yet), so RAM here is the whole 128 KB.
 *
 * Keep the flash origin/size in sync with BOOT_FLASH_* in
 * Console/Inc/Flasher/flash_map.h.
 */
ENTRY(Reset_Handler)

_estack = ORIGIN(BOOTLOADER_RAM) + LENGTH(BOOTLOADER_RAM); /* top of SRAM */
_Min_Heap_Size = 0x0;
_Min_Stack_Size = 0x800;

/* The bootloader owns its own, dedicated regions — it runs before the app, so it
 * has the whole 128 KB SRAM to itself. These deliberately do NOT come from
 * common.ld: BOOTLOADER_RAM spans the full SRAM, which the running console later
 * partitions into CONSOLE_RAM + GAME_RAM (the same bytes, used at a different time),
 * so they can't share one non-overlapping MEMORY map. Keep BOOTLOADER_FLASH in sync
 * with BOOT_FLASH_* in Console/Inc/Flasher/flash_map.h. */
MEMORY
{
    BOOTLOADER_FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K   /* sector 0 */
    BOOTLOADER_RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >BOOTLOADER_FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >BOOTLOADER_FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >BOOTLOADER_FLASH

  _sidata = LOADADDR(.data);

  /* .data + .RamFunc: linked in RAM, loaded from a flash LMA copy. The startup
   * copies them to RAM, so the flash erase/program primitives (.RamFunc) execute
   * from SRAM and keep running while the flash array is stalled mid-operation. */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >BOOTLOADER_RAM AT> BOOTLOADER_FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >BOOTLOADER_RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >BOOTLOADER_RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
