#include "crash_log.h"
#include "logger.h"
#include "sd_layout.h"
#include "ff.h"
#include <string.h>

void crashLogAppend(const char *const line)
{
    if (line == NULL)
    {
        return;
    }

    FIL f;
    const char *const path = SD_DIR_CRASHES "/crash.log";
    /* FA_OPEN_APPEND creates the file if missing and seeks to the end, so each crash
     * adds one line without rewriting the history. */
    if (f_open(&f, path, FA_WRITE | FA_OPEN_APPEND) != FR_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "crash log open '%s' failed", path);
        return;
    }

    UINT written = 0U;
    f_write(&f, line, (UINT)strlen(line), &written);
    f_write(&f, "\n", 1U, &written);
    f_close(&f);
    LOGGER_LOG_INFO(LOGGER_LOADER, "crash appended to %s", path);
}
