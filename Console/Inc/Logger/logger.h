#ifndef __LOGGER_H
#define __LOGGER_H

#include <stdarg.h>

typedef enum
{
    LOGGER_LEVEL_ERROR = 0,
    LOGGER_LEVEL_WARN,
    LOGGER_LEVEL_INFO,
    LOGGER_LEVEL_DEBUG,
} LoggerLevel;

#include "logger_config.h"

/* Core sink: format one line and emit it. Prefer the LOGGER_LOG_* macros. */
void loggerLog(LoggerLevel level, const char *channel, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* ConsoleAPI.log target: info-level logging for loaded games. */
void loggerGameLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#if LOGGER_ENABLED

#define LOGGER_LOG_DISPATCH(enabled, tag, level, fmt, ...)   \
    do                                                       \
    {                                                        \
        if ((enabled) && (level) <= LOGGER_MAX_LEVEL)        \
        {                                                    \
            loggerLog((level), (tag), (fmt), ##__VA_ARGS__); \
        }                                                    \
    } while (0)

#define LOGGER_LOG_ERROR(channel, fmt, ...) LOGGER_LOG_DISPATCH(channel, #channel, LOGGER_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOGGER_LOG_WARN(channel, fmt, ...) LOGGER_LOG_DISPATCH(channel, #channel, LOGGER_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOGGER_LOG_INFO(channel, fmt, ...) LOGGER_LOG_DISPATCH(channel, #channel, LOGGER_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOGGER_LOG_DEBUG(channel, fmt, ...) LOGGER_LOG_DISPATCH(channel, #channel, LOGGER_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

#else /* !LOGGER_ENABLED */

#define LOGGER_LOG_ERROR(channel, fmt, ...) ((void)0)
#define LOGGER_LOG_WARN(channel, fmt, ...) ((void)0)
#define LOGGER_LOG_INFO(channel, fmt, ...) ((void)0)
#define LOGGER_LOG_DEBUG(channel, fmt, ...) ((void)0)

#endif /* LOGGER_ENABLED */

#endif /* __LOGGER_H */
