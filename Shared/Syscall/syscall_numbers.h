#ifndef __SYSCALL_NUMBERS_H
#define __SYSCALL_NUMBERS_H

/*
 * Console syscall ABI — the single source of truth shared by the game-side stubs
 * (Shared/Syscall/console_syscalls.c) and the console-side dispatcher
 * (Console/Src/Kernel/syscall.c).
 *
 * A game never calls console code directly. Each API call is a strongly-typed C
 * wrapper (console_syscalls.h) that places the syscall id in r12 and the arguments
 * in r0-r3, then executes `svc #0`. The console traps into SVC_Handler (privileged,
 * on the kernel MSP stack), validates the arguments, runs the real console
 * function, and writes the result back into the caller's stacked r0.
 *
 * Bump CONSOLE_ABI_VERSION whenever the id assignments or argument marshalling
 * change — or the GameBinaryHeader layout does; the loader refuses a game built
 * against a different ABI. The value is a plain integer (no suffix) so it is
 * usable from both C and assembly.
 *
 * v5: GameBinaryHeader gained the stack_guard field (28 -> 32 bytes);
 *     rendererInit() retired from the game API and SYS_RENDERER_INIT removed from
 *     the enum (the kernel resets the renderer when it launches a game);
 *     the 15 per-button/per-axis SYS_JOY_* calls collapsed into one
 *     SYS_INPUT_GET_STATE (batched pad snapshot with edge events + raw axes);
 *     added SYS_GET_RANDOM (hardware TRNG) and SYS_BUZZER_SET_TIMBRE (per-track
 *     PWM duty / instrument timbre).
 * v6: added SYS_OS_TEXT_INPUT — the OS runs its on-screen keyboard modally on a
 *     game's behalf (e.g. high-score name entry) and fills a game buffer. The
 *     modal blocks for as long as the user types, so it exempts the calling
 *     callback from the liveness deadline for its duration (see the kernel's
 *     kernelSuspend/ResumeCallbackDeadline).
 * v7: removed SYS_BUZZER_SET_TIMBRE and the pulse-duty "timbre" feature — on the
 *     passive buzzer the duty barely altered the tone, so it wasn't worth the API
 *     surface; the buzzer is a fixed 50% square again, and buzzer_interface.h
 *     (which held only the timbre presets/range) was deleted.
 */

#define CONSOLE_ABI_VERSION 7

#ifndef __ASSEMBLER__

#include <stdint.h>
#include "stdbool.h"

typedef enum
{
    SYS_GET_SYSTIME = 0,
    SYS_DELAY,
    SYS_GET_DELTA_US,
    SYS_GET_RANDOM, /* 32-bit hardware TRNG value */

    SYS_BUZZER_GET_MAX_TRACKS,
    SYS_BUZZER_PLAY,
    SYS_BUZZER_PLAY_WITH_FLAG,
    SYS_BUZZER_PAUSE,
    SYS_BUZZER_RESUME,
    SYS_BUZZER_STOP,
    SYS_BUZZER_STOP_ALL,

    SYS_INPUT_GET_STATE, /* fill an InputState: held/pressed/released + analog axes, one trap */

    SYS_RENDERER_CLEAR,
    SYS_RENDERER_SET_BACKGROUND,
    SYS_RENDERER_SUBMIT_LAYER,
    SYS_RENDERER_RENDER,
    SYS_RENDERER_WIDTH,
    SYS_RENDERER_HEIGHT,
    SYS_RENDERER_SYSTEM_COLOR,

    SYS_ASSET_METADATA,
    SYS_ASSET_DATA,

    SYS_SETTINGS_READ,
    SYS_SETTINGS_WRITE,
    SYS_SETTINGS_CLEAR,

    SYS_FONT_GLYPH_W,
    SYS_FONT_GLYPH_H,
    SYS_FONT_GET,
    SYS_FONT_SIZE,
    SYS_FONT_SCALE,

    SYS_LOG,

    /* ---- OS UI services (the console runs a modal on the game's behalf) ---- */
    SYS_OS_TEXT_INPUT, /* run the on-screen keyboard modally; fill a game buffer */

    /* ---- multiplayer (ESP-NOW; the game drives the lobby, the OS owns the session) ---- */
    SYS_MP_GET_ROLE,
    SYS_MP_HOST_START,
    SYS_MP_JOIN_START,
    SYS_MP_SCAN_HOSTS,
    SYS_MP_JOIN,
    SYS_MP_STOP,
    SYS_MP_GET_SELF_INDEX,
    SYS_MP_GET_PLAYER_COUNT,
    SYS_MP_IS_CONNECTED,
    SYS_MP_GET_NAME,
    SYS_MP_GET_SELF_NAME,
    SYS_MP_SEND,
    SYS_MP_RECEIVE,

    /* ---- lifecycle / context switch (kernel-internal, not game-facing stubs) ----
     * The OS owns the game loop: it INVOKEs one callback (init/update/render) at a
     * time, each running as a short unprivileged excursion that traps back with
     * FRAME_DONE when it returns. EXIT ends the session for good. */
    SYS_EXIT,       /* game -> console: clean shutdown, end the session */
    SYS_INVOKE,     /* console -> game: run one callback unprivileged */
    SYS_FRAME_DONE, /* game -> console: the invoked callback returned */

    SYS_COUNT
} SyscallId;

/* A handful of calls have more than 4 register arguments; the stub marshals them
 * into one of these structs (in the game's own RAM) and passes a single pointer,
 * which the dispatcher validates and reads. */
typedef struct
{
    uint8_t track;
    bool is_looped;
    const uint16_t *notes_data;
    uint16_t notes_number;
    bool *on_done_flag;
} SyscallBuzzerPlayArgs;

#endif /* __ASSEMBLER__ */

#endif /* __SYSCALL_NUMBERS_H */
