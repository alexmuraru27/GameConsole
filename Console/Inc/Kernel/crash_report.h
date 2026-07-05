#ifndef __KERNEL_CRASH_REPORT_H
#define __KERNEL_CRASH_REPORT_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Last-crash record. faults.c decodes a game fault beautifully — but only to SWO,
 * which needs a debug probe. This keeps the essentials in a small RAM struct that
 * survives the (recoverable) crash, so the console can show them on the menu's crash
 * banner and append them to Crashes/crash.log on the SD card. That is enough to map
 * the fault back to a source line offline with tools/scripts/decode_crash.py — no
 * probe required, which is what makes community game debugging practical.
 *
 * Lifecycle: crashReportBeginSession() arms a fresh record (storing the game name)
 * as each game launches; faults.c fills it via crashReportCaptureFault() on a CPU
 * fault, or the loader marks it a hang via crashReportMarkHang() when the liveness
 * deadline abandons a stuck game (which faults never see). crashReportLast() reads
 * it back; the format helpers render the banner line and the log line.
 */

#define CRASH_GAME_NAME_MAX 32U

typedef enum
{
    CRASH_NONE = 0,  /* no crash this session */
    CRASH_FAULT = 1, /* a CPU fault — fault_kind + the registers below are valid */
    CRASH_HANG = 2,  /* a callback overran its time budget (no fault registers) */
} CrashKind;

typedef struct
{
    CrashKind kind;
    char game[CRASH_GAME_NAME_MAX]; /* .bin name of the game that was running */
    uint8_t fault_kind;             /* FaultKind (faults.h), valid when kind == CRASH_FAULT */
    uint32_t pc, lr, psr;           /* stacked context of the faulting instruction */
    uint32_t cfsr, hfsr;            /* configurable + hard fault status registers */
    uint32_t mmfar, bfar;           /* faulting addresses; *_valid says which apply */
    bool mmfar_valid, bfar_valid;
    uint32_t uptime_ms;             /* getSysTime() at the crash (no RTC on this board) */
} CrashReport;

/* Arm a fresh record for a launching game: kind = NONE, `game_name` stored. */
void crashReportBeginSession(const char *game_name);

/* Fill the record from a CPU fault. Called by faults.c in exception context, so it
 * only stores fields — no formatting, no I/O. `mmfar`/`bfar` are taken as-is; their
 * validity is derived from the CFSR *ARVALID bits. */
void crashReportCaptureFault(uint8_t fault_kind, uint32_t pc, uint32_t lr, uint32_t psr,
                             uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar);

/* Mark the current session a hang (the deadline abandoned it; no CPU fault fired). */
void crashReportMarkHang(void);

/* The last record. kind == CRASH_NONE means the last game did not crash. */
const CrashReport *crashReportLast(void);

/* Short human one-liner for the crash banner, e.g. "PC 0x2001A3F4 DACCVIOL" or
 * "hung (callback timeout)". Returns the number of chars written. */
int crashReportFormatBanner(char *buf, uint32_t size);

/* Full single line for crash.log: uptime, game, fault, every register, and all set
 * CFSR flags by name. decode_crash.py parses PC=/LR= out of it. Returns chars written. */
int crashReportFormatLine(char *buf, uint32_t size);

#endif /* __KERNEL_CRASH_REPORT_H */
