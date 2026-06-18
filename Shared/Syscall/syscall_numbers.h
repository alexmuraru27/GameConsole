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
 * change; the loader refuses a game built against a different ABI. The value is a
 * plain integer (no suffix) so it is usable from both C and assembly — a game's
 * startup.s includes this header to emit its binary header.
 */

#define CONSOLE_ABI_VERSION 1

#ifndef __ASSEMBLER__

#include <stdint.h>
#include "stdbool.h"

typedef enum
{
    SYS_GET_SYSTIME = 0,
    SYS_DELAY,

    SYS_BUZZER_GET_MAX_TRACKS,
    SYS_BUZZER_PLAY,
    SYS_BUZZER_PLAY_WITH_FLAG,
    SYS_BUZZER_PAUSE,
    SYS_BUZZER_RESUME,
    SYS_BUZZER_STOP,
    SYS_BUZZER_STOP_ALL,

    SYS_JOY_R_UP,
    SYS_JOY_R_RIGHT,
    SYS_JOY_R_DOWN,
    SYS_JOY_R_LEFT,
    SYS_JOY_L_UP,
    SYS_JOY_L_RIGHT,
    SYS_JOY_L_DOWN,
    SYS_JOY_L_LEFT,
    SYS_JOY_SPECIAL1,
    SYS_JOY_SPECIAL2,
    SYS_JOY_R_ANALOG_Y,
    SYS_JOY_R_ANALOG_X,
    SYS_JOY_L_ANALOG_Y,
    SYS_JOY_L_ANALOG_X,
    SYS_JOY_ANY_PRESSED,

    SYS_RENDERER_INIT,
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
    SYS_EXIT,   /* game -> console: clean shutdown */
    SYS_LAUNCH, /* console -> game: enter the game (kernel-internal, not a game stub) */

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
