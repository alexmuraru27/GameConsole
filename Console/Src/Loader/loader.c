#include "loader.h"
#include "diskio_integration.h"
#include "gpio.h"
#include "sysclock.h"
#include "logger.h"
#include "sd_layout.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

FIL s_active_file;
FILINFO s_active_fileinfo;

bool s_is_file_opened = false;

/* Mounted FatFs work area for the SD volume, plus the debounced card-detect
 * state. The card-detect line can bounce while a card is being seated, so a
 * presence change must hold steady for MEDIA_DEBOUNCE_MS before we act on it. */
static FATFS s_fatfs;
static bool s_media_present;     /* committed (mounted) presence */
static bool s_media_candidate;   /* raw reading awaiting debounce */
static uint32_t s_media_since;   /* when the candidate first appeared */
#define MEDIA_DEBOUNCE_MS 250U

/* Create the SD directory layout (idempotent: f_mkdir returns FR_EXIST if the
 * folder is already there). Called after every successful mount so the tree is
 * present for the game loader, downloader, and flasher to read/write. */
static void ensureDirs(void)
{
    f_mkdir(SD_DIR_GAMES);
    f_mkdir(SD_DIR_SETTINGS);
    f_mkdir(SD_DIR_FIRMWARE);
    f_mkdir(SD_DIR_MANIFESTS);
    f_mkdir(SD_DIR_CRASHES);
}

void loaderMediaInit(void)
{
    f_mount(&s_fatfs, "0:", 1U);
    ensureDirs();
    s_media_present = sdCardPresent();
    s_media_candidate = s_media_present;
    s_media_since = getSysTime();
}

bool loaderMediaSync(void)
{
    const bool raw = sdCardPresent();
    const uint32_t now = getSysTime();

    if (raw != s_media_candidate)
    {
        /* New raw reading: restart the debounce window. */
        s_media_candidate = raw;
        s_media_since = now;
        return false;
    }
    if (raw == s_media_present || (now - s_media_since) < MEDIA_DEBOUNCE_MS)
    {
        return false; /* unchanged, or not yet stable long enough */
    }

    /* Committed change. */
    s_media_present = raw;
    if (raw)
    {
        diskMarkUninitialized();      /* re-run SDIO init for the new card */
        f_mount(&s_fatfs, "0:", 1U);  /* remount and read its filesystem */
        ensureDirs();                 /* the new card may lack the layout */
        LOGGER_LOG_INFO(LOGGER_LOADER, "SD card inserted");
    }
    else
    {
        f_mount(NULL, "0:", 0U);      /* drop the stale mount */
        diskMarkUninitialized();
        LOGGER_LOG_INFO(LOGGER_LOADER, "SD card removed");
    }
    return true;
}

bool loaderMediaPresent(void)
{
    return s_media_present;
}

static bool isBinaryFile(const char *filename)
{
    const char *binary_extensions[] = {".bin", NULL};

    const char *dot_idx = strrchr(filename, '.');
    if (!dot_idx)
    {
        return false;
    }

    for (int i = 0; binary_extensions[i] != NULL; i++)
    {
        if (strcasecmp(dot_idx, binary_extensions[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

bool loaderIsFileOpened()
{
    return s_is_file_opened;
}

uint32_t loaderGetBinaryFilesNumberInDirectory(void)
{
    FRESULT res;
    DIR dir;
    FILINFO finfo;
    uint32_t file_count = 0U;

    res = f_opendir(&dir, SD_DIR_GAMES);
    if (res != FR_OK)
    {
        return 0U;
    }

    while (1)
    {
        res = f_readdir(&dir, &finfo);

        if (res != FR_OK || finfo.fname[0U] == 0U)
        {
            break;
        }

        if (finfo.fattrib & (AM_HID | AM_SYS | AM_DIR))
        {
            continue;
        }

        if (isBinaryFile(finfo.fname))
        {
            file_count++;
        }
    }

    f_closedir(&dir);
    return file_count;
}

FIL *loaderGetFile()
{
    if (s_is_file_opened)
    {
        return &s_active_file;
    }
    return NULL;
}

FILINFO *loaderGetFileInfo()
{
    if (s_is_file_opened)
    {
        return &s_active_fileinfo;
    }
    return NULL;
}

FRESULT loaderCloseFile()
{
    s_is_file_opened = false;
    return f_close(&s_active_file);
}

FRESULT loaderOpenFile(const uint32_t binary_index)
{
    FRESULT res;
    DIR dir;
    FILINFO finfo;
    uint32_t binary_file_index = 0U;

    res = f_opendir(&dir, SD_DIR_GAMES);
    if (res != FR_OK)
    {
        return res;
    }

    while (1)
    {
        res = f_readdir(&dir, &finfo);

        if (res != FR_OK)
        {
            f_closedir(&dir);
            return res;
        }

        if (finfo.fname[0U] == 0U)
        {
            f_closedir(&dir);
            return FR_NO_FILE;
        }

        if (finfo.fattrib & (AM_HID | AM_SYS | AM_DIR))
        {
            continue;
        }

        if (isBinaryFile(finfo.fname))
        {
            if (binary_file_index == binary_index)
            {
                /* f_readdir gives the bare name; open it under Games/. */
                char path[FF_LFN_BUF + sizeof(SD_DIR_GAMES) + 1U];
                snprintf(path, sizeof(path), "%s/%s", SD_DIR_GAMES, finfo.fname);
                res = f_open(&s_active_file, path, FA_READ);
                s_active_fileinfo = finfo;
                if (res == FR_OK)
                {
                    s_is_file_opened = true;
                }
                f_closedir(&dir);
                return res;
            }
            binary_file_index++;
        }
    }
}

FRESULT loaderGetFilenameByIndex(const uint32_t binary_index, char *const filename_out, uint32_t *const filename_length)
{
    FRESULT res;
    DIR dir;
    FILINFO finfo;
    uint32_t binary_file_index = 0U;

    if (!filename_out || !filename_length)
    {
        return FR_INVALID_PARAMETER;
    }

    // Clear output buffer
    filename_out[0] = '\0';
    *filename_length = 0U;

    res = f_opendir(&dir, SD_DIR_GAMES);
    if (res != FR_OK)
    {
        return res;
    }

    while (1)
    {
        res = f_readdir(&dir, &finfo);

        if (res != FR_OK)
        {
            f_closedir(&dir);
            return res;
        }

        if (finfo.fname[0U] == 0U)
        {
            f_closedir(&dir);
            return FR_NO_FILE;
        }

        if (finfo.fattrib & (AM_HID | AM_SYS | AM_DIR))
        {
            continue;
        }

        if (isBinaryFile(finfo.fname))
        {
            if (binary_file_index == binary_index)
            {
                uint32_t actual_length = strlen(finfo.fname);
                strncpy(filename_out, finfo.fname, FF_LFN_BUF - 1U);
                filename_out[FF_LFN_BUF - 1U] = '\0';
                *filename_length = actual_length;

                f_closedir(&dir);
                return FR_OK;
            }
            binary_file_index++;
        }
    }
}

uint32_t loaderGetMaxFilenameSize()
{
    return FF_LFN_BUF;
}

FRESULT loaderDeleteGame(const uint32_t binary_index)
{
    char name[FF_LFN_BUF];
    uint32_t len = 0U;
    FRESULT res = loaderGetFilenameByIndex(binary_index, name, &len);
    if (res != FR_OK)
    {
        return res;
    }

    char path[sizeof(SD_DIR_GAMES) + FF_LFN_BUF + 1U];

    /* Remove the paired asset pack first, best-effort: swap the .bin extension for
     * .pak on a copy of the name. Many games ship without one, so a missing .pak is
     * not an error — only the .bin removal below decides the result. */
    char pak[FF_LFN_BUF];
    strncpy(pak, name, sizeof(pak) - 1U);
    pak[sizeof(pak) - 1U] = '\0';
    char *dot = strrchr(pak, '.');
    if (dot != NULL)
    {
        strcpy(dot, ".pak");
        snprintf(path, sizeof(path), "%s/%s", SD_DIR_GAMES, pak);
        if (f_unlink(path) == FR_OK)
        {
            LOGGER_LOG_INFO(LOGGER_LOADER, "deleted asset pack '%s'", pak);
        }
    }

    snprintf(path, sizeof(path), "%s/%s", SD_DIR_GAMES, name);
    res = f_unlink(path);
    if (res == FR_OK)
    {
        LOGGER_LOG_INFO(LOGGER_LOADER, "deleted game '%s'", name);
    }
    else
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "delete '%s' failed (%d)", name, (int)res);
    }
    return res;
}

void loaderAppendCrashLog(const char *const line)
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
