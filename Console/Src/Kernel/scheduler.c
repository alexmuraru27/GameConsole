#include "Kernel/scheduler.h"
#include "Kernel/syscall.h"
#include "syscall_numbers.h"
#include "Kernel/mpu.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Peripherals/watchdog.h"
#include "Logger/logger.h"

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
static volatile bool s_game_hung = false; /* abandoned by the callback deadline, not a fault */
static uint32_t s_game_psp = 0U;

/*
 * Per-callback liveness guard. The MPU confines a game's *memory*; this confines
 * its *CPU time*. A game callback (the bootstrap, init, update or render) that
 * runs longer than this budget is treated as hung and abandoned through the same
 * path as a crash, so a game that loses control — an infinite loop, a deadlock on
 * its own state — cannot wedge the whole console. Games are cooperatively
 * scheduled (each callback must return promptly; a game self-paces with short
 * delay()s, not a multi-second block in one callback), so the budget is generous
 * next to a real frame (<16 ms) or an asset-heavy init, yet still recovers in
 * human time. The timed-out game returns to the menu as "crashed".
 * GAME_CALLBACK_BUDGET_MS lives in scheduler.h (shared with the SYS_DELAY cap).
 */
static volatile bool s_callback_running = false;
static volatile uint32_t s_callback_deadline_ms = 0U;
/* Set while a callback is legitimately blocked inside the kernel for longer than
 * the budget — a modal OS service the game requested (osTextInput's on-screen
 * keyboard), which the player drives at human speed. The deadline is paused for
 * the duration so the game isn't abandoned as "hung" while the modal is up. */
static volatile bool s_deadline_suspended = false;
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
    ExceptionFrame *frame = (ExceptionFrame *)(GAME_STACK_TOP - sizeof(ExceptionFrame));
    frame->r0 = 0U;
    frame->r1 = 0U;
    frame->r2 = 0U;
    frame->r3 = 0U;
    frame->r12 = 0U;
    frame->lr = s_game_frame_return; /* the game-side return trampoline (Thumb bit kept) */
    frame->pc = callback & ~0x1U;    /* Thumb state is in xPSR, not PC bit 0 */
    frame->xpsr = 0x01000000U;       /* T-bit set */

    s_game_psp = (uint32_t)frame;
    /* Arm the liveness deadline for this callback, then enter it. SysTick
     * (privileged, 1 ms, priority above the SVC trap) advances the clock and
     * checks the deadline while the unprivileged callback runs. */
    s_callback_deadline_ms = getSysTime() + GAME_CALLBACK_BUDGET_MS;
    s_callback_running = true;
    kernelTriggerInvoke();
    s_callback_running = false;
    /* --- resumes here once the callback returns or the game leaves --- */
}

/* Called from SysTick (privileged, every 1 ms). If the in-flight game callback
 * has overrun its budget, abandon it exactly like a crash: pend the leave so
 * PendSV tail-chains back to the parked console context. One-shot per overrun
 * (clears the running flag so it pends PendSV once), and only ever acts while a
 * game callback is actually executing. Silent — this runs in an ISR; the
 * recovery is logged once back in kernelRunGame. */
void kernelGameDeadlineTick(uint32_t now_ms)
{
    if (s_callback_running && s_game_active && !s_deadline_suspended &&
        (int32_t)(now_ms - s_callback_deadline_ms) >= 0)
    {
        s_callback_running = false;
        s_game_hung = true;
        kernelRequestLeave(true);
    }
}

/* Pause / resume the per-callback liveness deadline around a kernel-side modal the
 * running game asked for (osTextInput). The keyboard blocks for as long as the
 * player types — far beyond the 2 s budget — but that is cooperative waiting, not
 * a runaway game, so the deadline must not fire meanwhile. Resume re-arms a fresh
 * full budget so the time spent in the modal is not charged against whatever the
 * game does after the call returns. Bracket every suspend with a resume. (The IWDG
 * is fed separately from inside the keyboard's own loop.) */
void kernelSuspendCallbackDeadline(void)
{
    s_deadline_suspended = true;
}

void kernelResumeCallbackDeadline(void)
{
    s_callback_deadline_ms = getSysTime() + GAME_CALLBACK_BUDGET_MS;
    s_deadline_suspended = false;
}

void kernelRunGame(const GameBinaryHeader *header,
                   void (*collect)(void), void (*send)(void))
{
    s_game_frame_return = header->frame_return; /* keep Thumb bit: it is used as LR */
    s_game_crashed = false;
    s_game_hung = false;
    s_game_active = true;

    /* Drive the liveness deadline off the 1 ms SysTick without the timebase having
     * to know about the kernel (idempotent; the hook no-ops outside a game). */
    sysclockSetTickHook(kernelGameDeadlineTick);

    LOGGER_LOG_INFO(LOGGER_KERNEL, "launching game: init=0x%08lX update=0x%08lX render=0x%08lX guard=0x%08lX",
                    (unsigned long)(header->init & ~0x1U), (unsigned long)(header->update & ~0x1U),
                    (unsigned long)(header->render & ~0x1U), (unsigned long)header->stack_guard);
    mpuConfigureForGame(header->stack_guard);

    /* One-time C-runtime bootstrap (zero .bss, run ctors), then the game's init. */
    kernelInvokeGame(header->entry_point);
    kernelInvokeGame(header->init);

    /* The OS owns the loop: collect inbound console work, step update(), emit
     * outbound, then render() (during which an async round-trip armed by send()
     * completes). A clean gameExit() or a crash inside update clears s_game_active
     * (via PendSV) so we stop before rendering a game that is no longer there. */
    while (s_game_active)
    {
        /* Kick the last-resort watchdog once per frame. A game that hangs is
         * caught faster and gracefully by the per-callback deadline above; this
         * keeps the IWDG fed during normal play (and after a deadline abandon,
         * control returns to the menu loop, which keeps kicking). */
        watchdogKick();
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
    if (s_game_hung)
    {
        LOGGER_LOG_ERROR(LOGGER_KERNEL, "game hung (callback overran %u ms); recovered to console",
                         (unsigned)GAME_CALLBACK_BUDGET_MS);
    }
    else if (s_game_crashed)
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
    ExceptionFrame *f = (ExceptionFrame *)frame;
    const uint32_t id = f->r12; /* r12 carries the syscall id */

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
        f->r0 = svcDispatch(id, frame); /* normal syscall; result -> caller r0 */
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
