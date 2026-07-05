#include "crash_report.h"
#include "faults.h"   /* FaultKind names */
#include "sysclock.h" /* getSysTime */
#include <stm32f407xx.h>
#include <stdio.h>
#include <string.h>

/* The one record. In .bss, so it powers on as CRASH_NONE and survives the recoverable
 * game->console switch (no reset happens on a game crash). */
static CrashReport s_report;

/* CFSR sub-flags, most-diagnostic first: the banner shows the first one set, the log
 * line lists them all. Kept here (not shared with faults.c's SWO decode) because the
 * two sinks want different shapes — one primary flag vs. the full verbose dump. */
typedef struct
{
    uint32_t mask;
    const char *name;
} CfsrFlag;

static const CfsrFlag s_cfsr_flags[] = {
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
#define CFSR_FLAG_COUNT (sizeof(s_cfsr_flags) / sizeof(s_cfsr_flags[0]))

static const char *faultKindName(uint8_t kind)
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

/* First CFSR flag set, in the priority order above, or NULL if none. */
static const char *primaryFlag(uint32_t cfsr)
{
    for (uint32_t i = 0U; i < CFSR_FLAG_COUNT; i++)
    {
        if (cfsr & s_cfsr_flags[i].mask)
        {
            return s_cfsr_flags[i].name;
        }
    }
    return NULL;
}

void crashReportBeginSession(const char *const game_name)
{
    memset(&s_report, 0, sizeof(s_report)); /* kind = CRASH_NONE */
    if (game_name != NULL)
    {
        strncpy(s_report.game, game_name, sizeof(s_report.game) - 1U);
        s_report.game[sizeof(s_report.game) - 1U] = '\0';
    }
}

void crashReportCaptureFault(uint8_t fault_kind, uint32_t pc, uint32_t lr, uint32_t psr,
                             uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar)
{
    s_report.kind = CRASH_FAULT;
    s_report.fault_kind = fault_kind;
    s_report.pc = pc;
    s_report.lr = lr;
    s_report.psr = psr;
    s_report.cfsr = cfsr;
    s_report.hfsr = hfsr;
    s_report.mmfar = mmfar;
    s_report.bfar = bfar;
    s_report.mmfar_valid = (cfsr & SCB_CFSR_MMARVALID_Msk) != 0U;
    s_report.bfar_valid = (cfsr & SCB_CFSR_BFARVALID_Msk) != 0U;
    s_report.uptime_ms = getSysTime();
}

void crashReportMarkHang(void)
{
    s_report.kind = CRASH_HANG;
    s_report.uptime_ms = getSysTime();
}

const CrashReport *crashReportLast(void)
{
    return &s_report;
}

int crashReportFormatBanner(char *buf, uint32_t size)
{
    if (buf == NULL || size == 0U)
    {
        return 0;
    }
    if (s_report.kind == CRASH_HANG)
    {
        return snprintf(buf, size, "hung (callback timeout)");
    }
    if (s_report.kind == CRASH_FAULT)
    {
        const char *flag = primaryFlag(s_report.cfsr);
        return snprintf(buf, size, "PC 0x%08lX %s", (unsigned long)s_report.pc,
                        (flag != NULL) ? flag : faultKindName(s_report.fault_kind));
    }
    return snprintf(buf, size, "recovered");
}

int crashReportFormatLine(char *buf, uint32_t size)
{
    if (buf == NULL || size == 0U)
    {
        return 0;
    }

    const char *const name = (s_report.game[0] != '\0') ? s_report.game : "?";

    if (s_report.kind == CRASH_HANG)
    {
        int n = snprintf(buf, size, "[%lu] %s HANG callback-timeout",
                         (unsigned long)s_report.uptime_ms, name);
        return (n < 0) ? 0 : n;
    }

    int n = snprintf(buf, size,
                     "[%lu] %s %s PC=0x%08lX LR=0x%08lX PSR=0x%08lX CFSR=0x%08lX HFSR=0x%08lX MMFAR=0x%08lX BFAR=0x%08lX flags:",
                     (unsigned long)s_report.uptime_ms, name, faultKindName(s_report.fault_kind),
                     (unsigned long)s_report.pc, (unsigned long)s_report.lr, (unsigned long)s_report.psr,
                     (unsigned long)s_report.cfsr, (unsigned long)s_report.hfsr,
                     (unsigned long)s_report.mmfar, (unsigned long)s_report.bfar);
    if (n < 0)
    {
        return 0;
    }
    uint32_t off = (uint32_t)n;
    for (uint32_t i = 0U; i < CFSR_FLAG_COUNT && off < size - 1U; i++)
    {
        if (s_report.cfsr & s_cfsr_flags[i].mask)
        {
            int m = snprintf(buf + off, size - off, " %s", s_cfsr_flags[i].name);
            if (m < 0)
            {
                break;
            }
            off += (uint32_t)m;
        }
    }
    if (off >= size)
    {
        off = size - 1U;
    }
    return (int)off;
}
