#ifndef __KERNEL_SYSCALL_H
#define __KERNEL_SYSCALL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Console-side of the syscall ABI. The game traps via `svc #0` with the syscall
 * id in r12; SVC_Handler (in syscall.c) runs on the kernel MSP stack, validates,
 * dispatches to the real console function, and returns the result in the caller's
 * stacked r0.
 */

/* Configure the SVC/PendSV exception priorities. SVC runs at the lowest urgency
 * so a long syscall (e.g. a full render) executed on the kernel stack stays
 * preemptible by the buzzer/joystick timer ISRs. Call once at boot. */
void syscallInit(void);

/* Run one game syscall: `id` selects the call, frame[0..3] are the stacked r0-r3
 * arguments; the return value becomes the caller's r0. Called by the SVC handler
 * in the scheduler for every non-lifecycle syscall. */
uint32_t svcDispatch(uint32_t id, uint32_t *frame);

#endif /* __KERNEL_SYSCALL_H */
