#include "scheduler.h"
#include "syscall.h"
#include "syscall_numbers.h"
#include "mpu.h"

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
/* The EXC_RETURN captured when the console launched the game. Reused to resume the
 * console so the stacked-frame type (basic vs. FP-extended) matches exactly. */
static uint32_t s_console_exc_return = RETURN_TO_CONSOLE;

bool kernelGameActive(void) { return s_game_active; }
bool kernelGameCrashed(void) { return s_game_crashed; }

void kernelRequestLeave(bool crashed)
{
    s_leave_crashed = crashed;
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk; /* PendSV switches us back to the console */
}

/* Safety net: if a game's entry function ever returns instead of calling
 * gameExit(), fall through to a clean exit. */
static void gameEntryReturned(void)
{
    __asm volatile("mov r12, %0 \n svc #0 \n" : : "i"(SYS_EXIT) : "r12");
    for (;;)
    {
    }
}

/* Console -> game launch trampoline. Runs privileged on the MSP. The push parks
 * the console's callee-saved registers below the SVC-stacked frame; the matching
 * pop runs only after the game has left and PendSV has returned us here. */
__attribute__((naked)) static void kernelTriggerLaunch(void)
{
    __asm volatile(
        "push {r4-r11, lr}   \n"
        "mov r12, %0         \n"
        "svc #0              \n" /* SYS_LAUNCH: enter the game; returns here on leave */
        "pop {r4-r11, pc}    \n"
        : : "i"(SYS_LAUNCH) : "r12", "memory");
}

void kernelRunGame(uint32_t entry_point)
{
    /* Build the initial exception frame the launch unstacks into the game. */
    uint32_t *frame = (uint32_t *)(GAME_STACK_TOP - 32U);
    frame[0] = 0U; /* r0  */
    frame[1] = 0U; /* r1  */
    frame[2] = 0U; /* r2  */
    frame[3] = 0U; /* r3  */
    frame[4] = 0U; /* r12 */
    frame[5] = (uint32_t)gameEntryReturned;  /* LR  */
    frame[6] = entry_point & ~0x1U;          /* PC  (Thumb state is in xPSR) */
    frame[7] = 0x01000000U;                  /* xPSR: T-bit set */

    s_game_psp = (uint32_t)frame;
    s_game_crashed = false;
    s_game_active = true;

    mpuConfigureForGame();
    kernelTriggerLaunch();
    /* --- resumes here once the game has exited or crashed (via PendSV) --- */
}

/* Teardown shared by the clean-exit and crash paths. Runs in PendSV (privileged).
 * Returns the EXC_RETURN that resumes the parked console context on the MSP. */
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
    case SYS_LAUNCH:
        /* Console -> game: switch the PSP to the game's frame and drop the thread
         * to unprivileged. The console frame the hardware just stacked on the MSP
         * stays parked until we leave the game; remember how to resume it. */
        s_console_exc_return = exc_return;
        __set_PSP(s_game_psp);
        __set_CONTROL(__get_CONTROL() | 0x1U); /* nPRIV = 1 (unprivileged thread) */
        __ISB();
        return RETURN_TO_GAME;

    case SYS_EXIT:
        /* Clean shutdown: defer the switch to PendSV (tail-chains immediately). */
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
        "mrseq r0, msp       \n" /* launch SVC comes from the console (MSP) */
        "mrsne r0, psp       \n" /* game syscalls come from the game (PSP) */
        "mov r1, lr          \n"
        "push {r0, r1}       \n" /* keep 8-byte alignment across the call */
        "bl svcHandlerMain   \n" /* returns the EXC_RETURN to use in r0 */
        "add sp, sp, #8      \n"
        "bx r0               \n");
}

/* PendSV performs the actual switch back to the console for both clean exit and
 * crash recovery. */
uint32_t pendSvLeave(void) { return kernelLeaveGame(); }

__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "bl pendSvLeave \n"
        "bx r0          \n");
}
