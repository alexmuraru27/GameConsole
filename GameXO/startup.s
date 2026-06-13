/**
 ******************************************************************************
 * @file      startup.s
 * @brief     Game startup — called by the console loader after switching
 *            MSP to the game's CCM stack. Runs static constructors and
 *            calls main(). The loader handles the stack context switch.
 *
 *            Unlike the console startup, this has NO vector table —
 *            the console handles all interrupts and exceptions.
 ******************************************************************************
 */

    .syntax unified
    .cpu cortex-m4
    .fpu softvfp
    .thumb

    .global _game_start

    .section .text._game_start, "ax", %progbits
    .type _game_start, %function
_game_start:
    push {lr}

    /* Run C++ static constructors and __attribute__((constructor)) functions */
    bl __libc_init_array

    /* Call the game's entry point */
    bl main

    /* Return to the loader */
    pop {pc}
    .size _game_start, .-_game_start
