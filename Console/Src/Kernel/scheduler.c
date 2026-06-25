#include "scheduler.h"
#include "syscall.h"
#include "syscall_numbers.h"
#include "mpu.h"
#include "logger.h"

#include <stddef.h>
#include <stm32f407xx.h>

/* GAME_RAM bounds (common.ld) — the game's PSP starts at the top of this region. */
extern uint32_t __game_ram_start, __game_ram_size;
#define GAME_STACK_TOP ((uint32_t)&__game_ram_start + (uint32_t)&__game_ram_size)

/* EXC_RETURN values used to retarget exception returns. */
#define RETURN_TO_GAME 0xFFFFFFFDU    /* Thread mode, Process stack (the game) */
#define RETURN_TO_CONSOLE 0xFFFFFFF9U /* Thread mode, Main stack (the console) */

static volatile bool s_game_active = false;
static volatile bool s_game_crashed = false;
static volatile bool s_leave_crashed = false;
static uint32_t s_game_psp = 0U;
/* Return address (LR) installed in every invoked callback's frame — the game-side
 * _game_return trampoline that traps back here when a callback returns. */
static uint32_t s_game_frame_return = 0U;
/* The EXC_RETURN captured when the console invoked the game. Reused to resume the
 * console so the stacked-frame type (basic vs. FP-extended) matches exactly. */
static uint32_t s_console_exc_return = RETURN_TO_CONSOLE;

bool kernelGameActive(void) { return s_game_active; }
bool kernelGameCrashed(void) { return s_game_crashed; }

void kernelRequestLeave(bool crashed)
{
    s_leave_crashed = crashed;
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk; /* PendSV ends the session and switches back */
}

/* Console -> game invoke trampoline. Runs privileged on the MSP. The push parks
 * the console's callee-saved registers below the SVC-stacked frame; the matching
 * pop runs only after the callback has returned (SYS_FRAME_DONE) or the game has
 * left (SYS_EXIT / crash, via PendSV) and control has come back here. */
__attribute__((naked)) static void kernelTriggerInvoke(void)
{
    __asm volatile(
        "push {r4-r11, lr}   \n"
        "mov r12, %0         \n"
        "svc #0              \n" /* SYS_INVOKE: run one callback; returns here on leave */
        "pop {r4-r11, pc}    \n"
        : : "i"(SYS_INVOKE) : "r12", "memory");
}

/* Run one game callback (init/update/render or the bootstrap) unprivileged on a
 * fresh PSP frame, returning when it returns. A no-op once the session has ended,
 * so a crash/exit mid-loop cleanly skips the remaining callbacks. */
static void kernelInvokeGame(uint32_t callback)
{
    if (!s_game_active)
    {
        return;
    }

    /* Build the initial exception frame the invoke unstacks into the callback. */
    uint32_t *frame = (uint32_t *)(GAME_STACK_TOP - 32U);
    frame[0] = 0U;                  /* r0  */
    frame[1] = 0U;                  /* r1  */
    frame[2] = 0U;                  /* r2  */
    frame[3] = 0U;                  /* r3  */
    frame[4] = 0U;                  /* r12 */
    frame[5] = s_game_frame_return; /* LR: the game-side return trampoline (Thumb bit kept) */
    frame[6] = callback & ~0x1U;    /* PC  (Thumb state is in xPSR) */
    frame[7] = 0x01000000U;         /* xPSR: T-bit set */

    s_game_psp = (uint32_t)frame;
    kernelTriggerInvoke();
    /* --- resumes here once the callback returns or the game leaves --- */
}

void kernelRunGame(const GameBinaryHeader *header,
                   void (*collect)(void), void (*send)(void))
{
    s_game_frame_return = header->frame_return; /* keep Thumb bit: it is used as LR */
    s_game_crashed = false;
    s_game_active = true;

    LOGGER_LOG_INFO(LOGGER_KERNEL, "launching game: init=0x%08lX update=0x%08lX render=0x%08lX",
                    (unsigned long)(header->init & ~0x1U), (unsigned long)(header->update & ~0x1U),
                    (unsigned long)(header->render & ~0x1U));
    mpuConfigureForGame();

    /* One-time C-runtime bootstrap (zero .bss, run ctors), then the game's init. */
    kernelInvokeGame(header->entry_point);
    kernelInvokeGame(header->init);

    /* The OS owns the loop: collect inbound console work, step update(), emit
     * outbound, then render() (during which an async round-trip armed by send()
     * completes). A clean gameExit() or a crash inside update clears s_game_active
     * (via PendSV) so we stop before rendering a game that is no longer there. */
    while (s_game_active)
    {
        if (collect != NULL)
        {
            collect();
        }
        kernelInvokeGame(header->update);
        if (s_game_active && send != NULL)
        {
            send();
        }
        kernelInvokeGame(header->render);
    }

    /* Back in privileged thread context. Safe to log: the per-game lifecycle ends
     * here, not on a hot path. (The leave itself happens in PendSV, which stays
     * silent.) */
    if (s_game_crashed)
    {
        LOGGER_LOG_ERROR(LOGGER_KERNEL, "game crashed; recovered to console");
    }
    else
    {
        LOGGER_LOG_INFO(LOGGER_KERNEL, "game exited cleanly");
    }
}

/* Session teardown shared by the clean-exit and crash paths. Runs in PendSV
 * (privileged). Returns the EXC_RETURN that resumes the parked console context on
 * the MSP. */
static uint32_t kernelLeaveGame(void)
{
    s_game_crashed = s_leave_crashed;
    s_game_active = false;
    mpuReleaseGame();
    __set_CONTROL(__get_CONTROL() & ~0x1U); /* re-privilege the thread (console) */
    __ISB();
    return s_console_exc_return;
}

/* Called from the SVC trampoline. Returns the EXC_RETURN the handler should use. */
uint32_t svcHandlerMain(uint32_t *frame, uint32_t exc_return)
{
    const uint32_t id = frame[4]; /* r12 carries the syscall id */

    switch (id)
    {
    case SYS_INVOKE:
        /* Console -> game: switch the PSP to the callback's frame and drop the
         * thread to unprivileged. The console frame the hardware just stacked on
         * the MSP stays parked until the callback leaves; remember how to resume it. */
        s_console_exc_return = exc_return;
        __set_PSP(s_game_psp);
        __set_CONTROL(__get_CONTROL() | 0x1U); /* nPRIV = 1 (unprivileged thread) */
        __ISB();
        return RETURN_TO_GAME;

    case SYS_FRAME_DONE:
        /* Game -> console: a callback returned. Re-privilege the thread and resume
         * the parked console frame on the MSP directly — the MPU/session stay up so
         * the OS can invoke the next callback. */
        __set_CONTROL(__get_CONTROL() & ~0x1U);
        __ISB();
        return s_console_exc_return;

    case SYS_EXIT:
        /* Clean shutdown: end the session via PendSV (tail-chains immediately). */
        kernelRequestLeave(false);
        return exc_return;

    default:
        frame[0] = svcDispatch(id, frame); /* normal syscall; result -> caller r0 */
        return exc_return;
    }
}

__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile(
        "tst lr, #4          \n" /* EXC_RETURN bit 2: which stack was in use */
        "ite eq              \n"
        "mrseq r0, msp       \n" /* invoke SVC comes from the console (MSP) */
        "mrsne r0, psp       \n" /* game syscalls come from the game (PSP) */
        "mov r1, lr          \n"
        "push {r0, r1}       \n" /* keep 8-byte alignment across the call */
        "bl svcHandlerMain   \n" /* returns the EXC_RETURN to use in r0 */
        "add sp, sp, #8      \n"
        "bx r0               \n");
}

/* PendSV performs the actual switch back to the console for both clean exit and
 * crash recovery — it ends the session (releases the MPU). */
uint32_t pendSvLeave(void) { return kernelLeaveGame(); }

__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "bl pendSvLeave \n"
        "bx r0          \n");
}
