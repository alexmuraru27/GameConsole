/**
 ******************************************************************************
 * @file      startup.s
 * @brief     Game startup. Emits the 12-byte GameBinaryHeader at offset 0 of the
 *            .bin and defines the entry trampoline _game_start.
 *
 *            The console loader validates the header, copies the flat image to
 *            GAME_RAM, and (via the kernel) enters _game_start unprivileged on a
 *            fresh PSP. _game_start zeroes .bss, runs static constructors, calls
 *            main(), then traps back to the console with gameExit().
 *
 *            There is NO vector table — the console owns every interrupt and
 *            exception; a game only ever traps out through SVC.
 ******************************************************************************
 */

#include "header_interface.h"   /* GAME_BINARY_MAGIC, CONSOLE_ABI_VERSION (asm-safe) */

    .syntax unified
    .cpu cortex-m4
    .fpu softvfp
    .thumb

/* ---- Game binary header: the first 12 bytes of the .bin ----
 * Linked first in GAME_RAM by common.ld (KEEP(*(.game_header))). The loader reads
 * magic + ABI version to accept the game, then jumps to entry_point. */
    .section .game_header, "a", %progbits
    .p2align 2
    .global game_binary_header
game_binary_header:
    .word GAME_BINARY_MAGIC      /* "GAME" */
    .word CONSOLE_ABI_VERSION    /* must match the console's ABI */
    .word _game_start            /* entry (linker sets the Thumb bit) */
    .size game_binary_header, .-game_binary_header

/* ---- Entry trampoline ---- */
    .global _game_start
    .section .text._game_start, "ax", %progbits
    .type _game_start, %function
_game_start:
    push {lr}

    /* Zero .bss. The console loader copies only the flat image (.text/.rodata/
     * .data, already at their final RAM addresses) — .bss is NOLOAD, so the game
     * clears it itself, exactly as a normal reset handler would. */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
zero_bss_loop:
    cmp r0, r1
    bcs zero_bss_done
    str r2, [r0], #4
    b zero_bss_loop
zero_bss_done:

    /* Run C++ static constructors and __attribute__((constructor)) functions */
    bl __libc_init_array

    /* Call the game's entry point */
    bl main

    /* Return control to the console. The game runs unprivileged, so it cannot
     * just return into console code — it traps out through the exit syscall. */
    bl gameExit
    .size _game_start, .-_game_start
