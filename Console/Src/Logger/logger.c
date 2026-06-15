#include "logger.h"

#include <stdio.h>
#include <string.h>

#include "sysclock.h"

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
     * shared "LOGGER_" prefix here, then the "%.4s" below caps the printed tag
     * at four chars ("LOGGER_RENDERER" -> "REND") to bound the bytes pushed
     * over SWO. Tags without the prefix (e.g. "GAME") pass straight through. */
    static const char prefix[] = "LOGGER_";
    if (strncmp(channel, prefix, sizeof(prefix) - 1U) == 0)
    {
        channel += sizeof(prefix) - 1U;
    }

    printf("[%lu][%c][%.4s] ", (unsigned long)getSysTime(), loggerLevelChar(level), channel);
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
     * them here on the master switch and the LOGGER_GAME channel. Both are
     * compile-time constants, so a disabled game channel folds this body away. */
    if (LOGGER_ENABLED && LOGGER_GAME)
    {
        va_list args;
        va_start(args, fmt);
        loggerEmit(LOGGER_LEVEL_INFO, "GAME", fmt, args);
        va_end(args);
    }
}
