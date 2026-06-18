#ifndef __GAME_HEADER_API_H
#define __GAME_HEADER_API_H

#include "syscall_numbers.h" /* CONSOLE_ABI_VERSION (asm-safe) */

/*
 * Tiny prefix at offset 0 of every game .bin. Because a game is RAM-resident and
 * reaches the console through SVC traps (not a function-pointer table), the loader
 * does not need a section table: it validates this header, copies the whole flat
 * image to GAME_RAM (.text/.rodata/.data are already linked at their final
 * addresses), and jumps to entry_point. The game's _game_start zeroes its own
 * .bss before calling main().
 *
 * The header is emitted by the game's startup.s, which includes this file and
 * writes the three words into the `.game_header` section — see docu/game_template/.
 * Both constants are plain integers (no suffix) so the assembler can use them.
 */

#define GAME_BINARY_MAGIC 0x47414D45 /* "GAME" */

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct
{
    uint32_t magic;       /* GAME_BINARY_MAGIC */
    uint32_t abi_version; /* must equal CONSOLE_ABI_VERSION, or the loader refuses it */
    uint32_t entry_point; /* address of _game_start in GAME_RAM */
} GameBinaryHeader;

#endif /* __ASSEMBLER__ */

#endif /* __GAME_HEADER_API_H */
