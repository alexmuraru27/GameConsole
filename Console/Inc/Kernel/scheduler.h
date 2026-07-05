#ifndef __KERNEL_SCHEDULER_H
#define __KERNEL_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "header_interface.h"

/*
 * The game/console context switch.
 *
 * The console runs privileged on the MSP and OWNS the game loop. To run a game
 * the kernel programs the MPU once, then drives the game's callbacks one at a
 * time: each invoke builds an unprivileged context on the PSP and switches in via
 * an SVC (parking the console's context on the MSP); when the callback returns it
 * traps straight back out (SYS_FRAME_DONE) to the parked console context, leaving
 * the MPU/session up so the next callback can run. The session ends — MPU torn
 * down, control returned for good — via PendSV, pended either by the game's clean
 * gameExit() (SYS_EXIT) or by the fault handler after a crash. A crash is
 * recoverable precisely because the faulting (unprivileged, PSP) context can be
 * abandoned: PendSV tail-chains before the faulting instruction is retried.
 */

/* Run a loaded game, OS-driven: bootstrap + init, then loop
 * {collect; update; send; render} until it exits or crashes. `collect` and `send`
 * (either may be NULL) run privileged around update() for console background work:
 * `collect` ingests inbound state before the game steps, `send` emits outbound
 * after it — split so an async round-trip (the ESP-NOW poll) overlaps render()
 * rather than blocking the loop. Programs/releases the MPU around the session;
 * returns once the game is gone. */
void kernelRunGame(const GameBinaryHeader *header,
                   void (*collect)(void), void (*send)(void));

/* Whether the most recent kernelRunGame() ended in a crash. */
bool kernelGameCrashed(void);

/* True while a game (unprivileged) context is active. The fault handler uses this
 * to decide whether a fault is a recoverable game crash or a fatal kernel fault. */
bool kernelGameActive(void);

/* Request a switch back to the console (pends PendSV) and END the session. Called
 * by the SYS_EXIT syscall (crashed = false) and the fault handler on a game crash
 * (true). */
void kernelRequestLeave(bool crashed);

/* Liveness tick, called from SysTick (1 ms). If the game callback currently
 * executing has run past its time budget, abandon it like a crash so a runaway
 * game (e.g. an infinite loop, which faults never catch) can't wedge the console.
 * No-op when no game callback is in flight. */
void kernelGameDeadlineTick(uint32_t now_ms);

/* Pause / resume the per-callback liveness deadline while a game callback is
 * legitimately blocked in a kernel-side modal it requested (the osTextInput
 * keyboard). The player drives the modal at human speed, so the 2 s budget must
 * not fire; resume re-arms a fresh budget for whatever runs after the call.
 * Always pair a suspend with a resume. */
void kernelSuspendCallbackDeadline(void);
void kernelResumeCallbackDeadline(void);

#endif /* __KERNEL_SCHEDULER_H */
