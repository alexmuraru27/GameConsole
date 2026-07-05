#ifndef __GAME_HEADER_API_H
#define __GAME_HEADER_API_H

#include "syscall_numbers.h" /* CONSOLE_ABI_VERSION (asm-safe) */

/*
 * Tiny prefix at offset 0 of every game .bin. Because a game is RAM-resident and
 * reaches the console through SVC traps (not a function-pointer table), the loader
 * does not need a section table: it validates this header, copies the whole flat
 * image to GAME_RAM (.text/.rodata/.data are already linked at their final
 * addresses), and drives the game through the callbacks named here.
 *
 * The OS owns the game loop. It first invokes `entry_point` (the C-runtime
 * bootstrap: zero .bss, run constructors), then `init` once, then loops `update`
 * then `render` every frame — servicing its own background work in between. Each
 * callback runs as a short unprivileged excursion and returns to the OS through
 * `frame_return`, the game-side trap stub the kernel installs as the callback's
 * return address.
 *
 * A game declares this header with DECLARE_GAME_HEADER(init, update, render) at
 * file scope in its main.c — see docu/game_template/. The macro fills the magic,
 * ABI version, and the two runtime stubs (entry_point/frame_return, both provided
 * by Shared/Syscall/console_syscalls.c); the game only names its three callbacks.
 */

#define GAME_BINARY_MAGIC 0x47414D45 /* "GAME" */

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct
{
    uint32_t magic;        /* GAME_BINARY_MAGIC */
    uint32_t abi_version;  /* must equal CONSOLE_ABI_VERSION, or the loader refuses it */
    uint32_t entry_point;  /* _game_start: one-time C-runtime bootstrap */
    uint32_t frame_return; /* _game_return: callback return trampoline (LR for each invoke) */
    uint32_t init;         /* game init  — OS calls once, after the bootstrap */
    uint32_t update;       /* game update — OS calls every frame */
    uint32_t render;       /* game render — OS calls every frame */
    uint32_t stack_guard;  /* base of the 256-byte stack-overflow guard band (below the PSP) */
} GameBinaryHeader;

/* The runtime stubs the macro wires into the header. Both live in the shared
 * syscall library compiled into every game (console_syscalls.c); a game never
 * references them by name. */
void _game_start(void);  /* zero .bss + run constructors, then return to the OS */
void _game_return(void); /* trap back to the OS when a callback returns */

/* Base of the game's stack-overflow guard band, defined by the app linker script
 * (app.ld). The kernel maps this 256-byte band as a no-access MPU region just
 * below the descending PSP so an overflow faults instead of corrupting .bss.
 * Only referenced inside DECLARE_GAME_HEADER (a game-side construct); the console
 * never expands the macro, so its link never needs this symbol. */
extern uint32_t __stack_guard_start[];

/*
 * Emit the binary header. Use once at file scope in the game's main.c, e.g.
 *   DECLARE_GAME_HEADER(gameInit, gameUpdate, gameRender)
 * The struct lands at offset 0 of the .bin via the .game_header section (KEEP'd by
 * common.ld). Function names decay to pointers with the Thumb bit set; the kernel
 * masks it for the PC and keeps it for the return address.
 */
#define DECLARE_GAME_HEADER(init_fn, update_fn, render_fn)         \
    __attribute__((section(".game_header"), used))                 \
    const GameBinaryHeader g_game_binary_header = {                \
        .magic = GAME_BINARY_MAGIC,                                \
        .abi_version = CONSOLE_ABI_VERSION,                        \
        .entry_point = (uint32_t)&_game_start,                     \
        .frame_return = (uint32_t)&_game_return,                   \
        .init = (uint32_t)(init_fn),                               \
        .update = (uint32_t)(update_fn),                           \
        .render = (uint32_t)(render_fn),                           \
        .stack_guard = (uint32_t)__stack_guard_start,             \
    }

#endif /* __ASSEMBLER__ */

#endif /* __GAME_HEADER_API_H */
