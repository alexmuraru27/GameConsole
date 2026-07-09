#ifndef __KERNEL_FAULTS_H
#define __KERNEL_FAULTS_H

#include <stdint.h>

/* Which fault vector trapped. Passed by the naked exception handlers (which
 * also hand over the faulting stack frame and EXC_RETURN) to faultReport(). */
typedef enum
{
    FAULT_HARD = 0,
    FAULT_MEMMANAGE = 1,
    FAULT_BUS = 2,
    FAULT_USAGE = 3,
} FaultKind;

/* Human name for a FaultKind ("MEMMANAGE"/"BUSFAULT"/"USAGEFAULT"/"HARDFAULT").
 * Shared by the SWO decode (faults.c) and the persisted crash report. */
const char *faultKindName(FaultKind kind);

/* One CFSR sub-flag: its status-register mask and printable name. */
typedef struct
{
    uint32_t mask;
    const char *name;
} CfsrFlag;

/* The CFSR sub-flags, ordered most-diagnostic-first (MemManage before Bus before
 * Usage; within a group the likeliest game bug first). One table shared by every
 * consumer: faults.c's full SWO dump, and crash_report.c's primary-flag banner +
 * full log line. `g_cfsr_flag_count` is its length. */
extern const CfsrFlag g_cfsr_flags[];
extern const uint32_t g_cfsr_flag_count;

/* Enable the configurable faults (MemManage/Bus/Usage) so an MPU violation, bus
 * error, or undefined instruction surfaces as its own fault — correctly named —
 * instead of escalating into a generic forced HardFault. Also traps divide-by-zero.
 * Call once during boot, after swoInit() so the report can reach SWO. */
void faultsInit(void);

/* Decode a fault and print the cause over SWO: fault name, the faulting context
 * (PSP/thread = the game, or MSP/handler = the kernel), stacked PC/LR/PSR, the
 * CFSR sub-flags by name, and MMFAR/BFAR when valid. `frame` is the 8-word
 * exception frame (r0-r3, r12, lr, pc, xpsr) on whichever stack faulted.
 *
 * A fault in a running game (unprivileged, PSP) is recoverable: this requests a
 * switch back to the console and returns the EXC_RETURN the handler should use.
 * A fault in the kernel itself is fatal and never returns (halts). */
uint32_t faultReport(uint32_t *frame, uint32_t exc_return, FaultKind kind);

#endif /* __KERNEL_FAULTS_H */
