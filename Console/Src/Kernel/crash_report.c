#include "Kernel/crash_report.h"
#include "Peripherals/systime.h"
#include "Kernel/faults.h"   /* FaultKind names */
#include "Peripherals/sysclock.h" /* getSysTime */
#include <stm32f407xx.h>
#include <stdio.h>
#include <string.h>

/* The one record. In .bss, so it powers on as CRASH_NONE and survives the recoverable
 * game->console switch (no reset happens on a game crash). */
static CrashReport s_report;

/* The CFSR flag table and faultKindName() are shared with faults.c (see faults.h),
 * so a flag or fault name is defined once. The banner shows the first flag set in
 * the table's diagnostic-priority order; the log line lists them all. */

/* First CFSR flag set, in the shared priority order, or NULL if none. */
static const char *primaryFlag(uint32_t cfsr)
{
    for (uint32_t i = 0U; i < g_cfsr_flag_count; i++)
    {
        if (cfsr & g_cfsr_flags[i].mask)
        {
            return g_cfsr_flags[i].name;
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
    for (uint32_t i = 0U; i < g_cfsr_flag_count && off < size - 1U; i++)
    {
        if (s_report.cfsr & g_cfsr_flags[i].mask)
        {
            int m = snprintf(buf + off, size - off, " %s", g_cfsr_flags[i].name);
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
