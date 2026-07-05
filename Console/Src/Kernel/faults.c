#include "faults.h"
#include "scheduler.h"
#include "crash_report.h"
#include <stdbool.h>
#include <stdio.h>
#include <stm32f407xx.h>

void faultsInit(void)
{
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk; /* divide-by-zero traps as a UsageFault */
    __DSB();
    __ISB();
}

static const char *faultName(FaultKind kind)
{
    switch (kind)
    {
    case FAULT_MEMMANAGE:
        return "MEMMANAGE";
    case FAULT_BUS:
        return "BUSFAULT";
    case FAULT_USAGE:
        return "USAGEFAULT";
    default:
        return "HARDFAULT";
    }
}

static void printFlag(uint32_t cfsr, uint32_t mask, const char *name)
{
    if (cfsr & mask)
    {
        printf(" %s", name);
    }
}

uint32_t faultReport(uint32_t *frame, uint32_t exc_return, FaultKind kind)
{
    const uint32_t cfsr = SCB->CFSR;
    /* EXC_RETURN bit 2 set => the faulting context was using the PSP, i.e. a
     * thread (the game). Clear => the kernel/handler faulted on the MSP. */
    const bool from_psp = (exc_return & 0x4U) != 0U;

    printf("\n=== %s FAULT ===\n", faultName(kind));
    printf("ctx =%s (EXC_RETURN=0x%08lX)\n", from_psp ? "PSP/thread" : "MSP/handler", (unsigned long)exc_return);
    printf("PC  =0x%08lX\n", (unsigned long)frame[6]);
    printf("LR  =0x%08lX\n", (unsigned long)frame[5]);
    printf("PSR =0x%08lX\n", (unsigned long)frame[7]);
    printf("CFSR=0x%08lX HFSR=0x%08lX\n", (unsigned long)cfsr, (unsigned long)SCB->HFSR);

    printf("flags:");
    /* MemManage (MMFSR, CFSR bits 0-7) */
    printFlag(cfsr, SCB_CFSR_IACCVIOL_Msk, "IACCVIOL");
    printFlag(cfsr, SCB_CFSR_DACCVIOL_Msk, "DACCVIOL");
    printFlag(cfsr, SCB_CFSR_MUNSTKERR_Msk, "MUNSTKERR");
    printFlag(cfsr, SCB_CFSR_MSTKERR_Msk, "MSTKERR");
    printFlag(cfsr, SCB_CFSR_MLSPERR_Msk, "MLSPERR");
    /* BusFault (BFSR, CFSR bits 8-15) */
    printFlag(cfsr, SCB_CFSR_IBUSERR_Msk, "IBUSERR");
    printFlag(cfsr, SCB_CFSR_PRECISERR_Msk, "PRECISERR");
    printFlag(cfsr, SCB_CFSR_IMPRECISERR_Msk, "IMPRECISERR");
    printFlag(cfsr, SCB_CFSR_UNSTKERR_Msk, "UNSTKERR");
    printFlag(cfsr, SCB_CFSR_STKERR_Msk, "STKERR");
    printFlag(cfsr, SCB_CFSR_LSPERR_Msk, "LSPERR");
    /* UsageFault (UFSR, CFSR bits 16-31) */
    printFlag(cfsr, SCB_CFSR_UNDEFINSTR_Msk, "UNDEFINSTR");
    printFlag(cfsr, SCB_CFSR_INVSTATE_Msk, "INVSTATE");
    printFlag(cfsr, SCB_CFSR_INVPC_Msk, "INVPC");
    printFlag(cfsr, SCB_CFSR_NOCP_Msk, "NOCP");
    printFlag(cfsr, SCB_CFSR_UNALIGNED_Msk, "UNALIGNED");
    printFlag(cfsr, SCB_CFSR_DIVBYZERO_Msk, "DIVBYZERO");
    printf("\n");

    if (cfsr & SCB_CFSR_MMARVALID_Msk)
    {
        printf("MMFAR=0x%08lX\n", (unsigned long)SCB->MMFAR);
    }
    if (cfsr & SCB_CFSR_BFARVALID_Msk)
    {
        printf("BFAR=0x%08lX\n", (unsigned long)SCB->BFAR);
    }

    /* A fault in a running game (unprivileged, PSP) is recoverable: abandon the
     * game and switch back to the console. We pend PendSV and return toward the
     * game with the original EXC_RETURN — PendSV tail-chains before the faulting
     * instruction can retry, so it never re-faults. */
    if (from_psp && kernelGameActive())
    {
        printf(">>> game crashed; returning to console\n");
        /* Persist the essentials before we clear the sticky bits — the menu shows
         * them on the crash banner and appends them to Crashes/crash.log, so the
         * fault can be decoded offline without a probe (tools/scripts/decode_crash.py).
         * MMFAR/BFAR must be read now, before the W1C below invalidates them. */
        crashReportCaptureFault((uint8_t)kind, frame[6], frame[5], frame[7],
                                cfsr, SCB->HFSR, SCB->MMFAR, SCB->BFAR);
        SCB->CFSR = cfsr;       /* write-1-to-clear the sticky fault bits */
        SCB->HFSR = SCB->HFSR;
        kernelRequestLeave(true);
        return exc_return;
    }

    /* A fault in the kernel itself is a real bug — halt for the debugger. */
    while (1)
    {
    }
}
