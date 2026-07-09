#ifndef __LOADER_CRASH_LOG_H
#define __LOADER_CRASH_LOG_H

/* Append one line (a newline is added) to Crashes/crash.log on the SD card,
 * creating it if needed. Best-effort: a failure is logged but not surfaced — crash
 * recovery must not fail on a full/absent card. The game loader calls this with the
 * decoded crash-report line after a game faults or hangs. */
void crashLogAppend(const char *line);

#endif /* __LOADER_CRASH_LOG_H */
