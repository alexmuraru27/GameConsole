INCLUDE "../common.ld"

/* Game code + data + stack all live in GAME_RAM (regular SRAM, I-bus + D-bus).
 * Assets live in GAME_RAM_ASSET (CCM, D-bus only — data, not code). */
_estack = ORIGIN(GAME_RAM) + LENGTH(GAME_RAM);    /* top of SRAM (0x20020000) */
_Min_Heap_Size = 0x0;
_Min_Stack_Size = 0x400;

SECTIONS
{
  .text :
  {
    . = ALIGN(4);
    __game_text_start = .;
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    __game_text_end = .;
    _etext = .;
  } >GAME_RAM

  .rodata :
  {
    . = ALIGN(4);
    __game_ro_data_start = .;
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    __game_ro_data_end = .;
  } >GAME_RAM

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    __game_data_init_start = .;
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)

    . = ALIGN(4);
    _edata = .;
    __game_data_init_end = .;
  } >GAME_RAM

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    __game_data_no_init_start = .;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
    __game_data_no_init_end = .;
  } >GAME_RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >GAME_RAM

  /* Asset arena in CCM — runtime buffer pool for .pak-loaded assets, not in .bin */
  .asset_area (NOLOAD):
  {
    . = ALIGN(4);
    __asset_area_start = .;
    . = . + LENGTH(GAME_RAM_ASSET);
    __asset_area_end = .;
  } >GAME_RAM_ASSET

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

}

ASSERT(__game_header_end <= __game_text_start, "Linker symbols order should be preserved in memory");
ASSERT(__game_text_end <= __game_ro_data_start, "Linker symbols order should be preserved in memory");
ASSERT(__game_ro_data_end <= __game_data_init_start, "Linker symbols order should be preserved in memory");
ASSERT(__game_data_init_end <= __game_data_no_init_start, "Linker symbols order should be preserved in memory");
