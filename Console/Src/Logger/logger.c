#include "Logger/logger.h"

#include <stdio.h>
#include <string.h>

#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"

static char loggerLevelChar(LoggerLevel level)
{
    /* ERROR/WARN/INFO/DEBUG -> E/W/I/D. The mask keeps the index in range so a
     * stray level can never read past the literal. */
    return "EWID"[(unsigned)level & 3U];
}

/* Shared formatter: "[<tick>][<L>][<CHAN>] <message>". The line is kept short
 * on purpose: each byte spin-waits ~5 us on the 2 MHz SWO link, so the prefix
 * cost is dominated by its length, not by the formatting. This is also the one
 * place that touches va_list, so loggerLog() and loggerGameLog() stay trivial. */
static void loggerEmit(LoggerLevel level, const char *channel, const char *fmt, va_list args)
{
    /* Channel tags arrive as the macro's stringized switch name. Drop the
     * shared "LOGGER_" prefix here and print the remainder in full
     * ("LOGGER_JOYSTICK" -> "JOYSTICK"). Tags without the prefix (e.g. "GAME")
     * pass straight through. */
    static const char prefix[] = "LOGGER_";
    if (strncmp(channel, prefix, sizeof(prefix) - 1U) == 0)
    {
        channel += sizeof(prefix) - 1U;
    }

    printf("[%7lu][%c][%s] ", (unsigned long)getSysTime(), loggerLevelChar(level), channel);
    vprintf(fmt, args);
    printf("\r\n");
}

void loggerLog(LoggerLevel level, const char *channel, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    loggerEmit(level, channel, fmt, args);
    va_end(args);
}

void loggerGameLog(const char *fmt, ...)
{
    /* Games share the console's logging config but cannot reference it; gate
     * them here exactly as the LOGGER_LOG_* macros would for an INFO-severity
     * site on the GAME channel. All operands are compile-time constants, so a
     * GAME level below INFO folds this whole body away. */
    if (LOGGER_ENABLED && LOGGER_LEVEL_INFO <= LOGGER_GAME && LOGGER_LEVEL_INFO <= LOGGER_MAX_LEVEL)
    {
        va_list args;
        va_start(args, fmt);
        loggerEmit(LOGGER_LEVEL_INFO, "GAME", fmt, args);
        va_end(args);
    }
}
