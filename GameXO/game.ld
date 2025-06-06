INCLUDE "../common.ld"

/* Highest address of the user mode stack */
_estack = ORIGIN(GAME_RAM_DATA) + LENGTH(GAME_RAM_DATA);    /* end of GAME_RAM_DATA */
/* Generate a link error if heap and stack don't fit into GAME_RAM_DATA */
_Min_Heap_Size = 0x0;  /* required amount of heap=0 -> use only static allocated memory  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Define output sections */
SECTIONS
{
  /* The program code and other data goes into GAME_RAM_EXEC */
  .text :
  {
    . = ALIGN(4);
    __game_text_start = .;
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    __game_text_end = .;
    _etext = .;        /* define a global symbols at end of code */
  } >GAME_RAM_EXEC

  /* Constant data goes into GAME_RAM_EXEC */
  .rodata :
  {
    . = ALIGN(4);
    __game_ro_data_start = .;
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
    __game_ro_data_end = .;
  } >GAME_RAM_EXEC

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into GAME_RAM_DATA, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    __game_data_init_start = .;
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
    __game_data_init_end = .;
  } >GAME_RAM_DATA

  /* Uninitialized data section */
  .bss :
  {
    . = ALIGN(4);
    /* This is used by the startup in order to initialize the .bss secion */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    __game_data_no_init_start = .;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
    __game_data_no_init_end = .;
  } >GAME_RAM_DATA

  /* User_heap_stack section, used to check that there is enough GAME_RAM_DATA left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >GAME_RAM_DATA

  .assets :
  {
    . = ALIGN(4);
    __game_code_assets_start = .;
    KEEP (*(.assets*)) 
    . = ALIGN(4);
    __game_code_assets_end = .;
  } >GAME_RAM_ASSETS

  /* Remove information from the standard libraries */
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
ASSERT(__game_data_no_init_end <= __game_code_assets_start, "Linker symbols order should be preserved in memory");