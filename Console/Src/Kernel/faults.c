#include "Kernel/faults.h"
#include "Kernel/scheduler.h"
#include "Kernel/crash_report.h"
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

const char *faultKindName(FaultKind kind)
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

const CfsrFlag g_cfsr_flags[] = {
    /* MemManage (a bad pointer / MPU violation — the common game bug) */
    {SCB_CFSR_DACCVIOL_Msk, "DACCVIOL"},
    {SCB_CFSR_IACCVIOL_Msk, "IACCVIOL"},
    {SCB_CFSR_MSTKERR_Msk, "MSTKERR"},
    {SCB_CFSR_MUNSTKERR_Msk, "MUNSTKERR"},
    {SCB_CFSR_MLSPERR_Msk, "MLSPERR"},
    /* BusFault */
    {SCB_CFSR_PRECISERR_Msk, "PRECISERR"},
    {SCB_CFSR_IMPRECISERR_Msk, "IMPRECISERR"},
    {SCB_CFSR_IBUSERR_Msk, "IBUSERR"},
    {SCB_CFSR_UNSTKERR_Msk, "UNSTKERR"},
    {SCB_CFSR_STKERR_Msk, "STKERR"},
    {SCB_CFSR_LSPERR_Msk, "LSPERR"},
    /* UsageFault */
    {SCB_CFSR_UNDEFINSTR_Msk, "UNDEFINSTR"},
    {SCB_CFSR_INVSTATE_Msk, "INVSTATE"},
    {SCB_CFSR_INVPC_Msk, "INVPC"},
    {SCB_CFSR_NOCP_Msk, "NOCP"},
    {SCB_CFSR_UNALIGNED_Msk, "UNALIGNED"},
    {SCB_CFSR_DIVBYZERO_Msk, "DIVBYZERO"},
};
const uint32_t g_cfsr_flag_count = sizeof(g_cfsr_flags) / sizeof(g_cfsr_flags[0]);

uint32_t faultReport(uint32_t *frame, uint32_t exc_return, FaultKind kind)
{
    const ExceptionFrame *f = (const ExceptionFrame *)frame;
    const uint32_t cfsr = SCB->CFSR;
    /* EXC_RETURN bit 2 set => the faulting context was using the PSP, i.e. a
     * thread (the game). Clear => the kernel/handler faulted on the MSP. */
    const bool from_psp = (exc_return & 0x4U) != 0U;

    printf("\n=== %s FAULT ===\n", faultKindName(kind));
    printf("ctx =%s (EXC_RETURN=0x%08lX)\n", from_psp ? "PSP/thread" : "MSP/handler", (unsigned long)exc_return);
    printf("PC  =0x%08lX\n", (unsigned long)f->pc);
    printf("LR  =0x%08lX\n", (unsigned long)f->lr);
    printf("PSR =0x%08lX\n", (unsigned long)f->xpsr);
    printf("CFSR=0x%08lX HFSR=0x%08lX\n", (unsigned long)cfsr, (unsigned long)SCB->HFSR);

    printf("flags:");
    for (uint32_t i = 0U; i < g_cfsr_flag_count; i++)
    {
        if (cfsr & g_cfsr_flags[i].mask)
        {
            printf(" %s", g_cfsr_flags[i].name);
        }
    }
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
        crashReportCaptureFault((uint8_t)kind, f->pc, f->lr, f->xpsr,
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
