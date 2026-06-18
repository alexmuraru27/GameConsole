#ifndef __KERNEL_SCHEDULER_H
#define __KERNEL_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * The game/console context switch.
 *
 * The console runs privileged on the MSP. To run a game, the kernel builds the
 * game's initial unprivileged context on its PSP and switches in via an SVC
 * (which parks the console's context on the MSP). Control comes back to the
 * console — at the exact point it left — via PendSV, pended either by the game's
 * clean exit (SYS_EXIT) or by the fault handler after a crash. A crash is
 * recoverable precisely because the faulting (unprivileged, PSP) context can be
 * abandoned: PendSV tail-chains before the faulting instruction is retried.
 */

/* Run a loaded game at entry_point. Returns when the game exits or crashes; the
 * console resumes privileged on the MSP. Enables/disables the MPU around the run. */
void kernelRunGame(uint32_t entry_point);

/* Whether the most recent kernelRunGame() ended in a crash. */
bool kernelGameCrashed(void);

/* True while a game (unprivileged) context is active. The fault handler uses this
 * to decide whether a fault is a recoverable game crash or a fatal kernel fault. */
bool kernelGameActive(void);

/* Request a switch back to the console (pends PendSV). Called by the SYS_EXIT
 * syscall (crashed = false) and by the fault handler on a game crash (true). */
void kernelRequestLeave(bool crashed);

#endif /* __KERNEL_SCHEDULER_H */
