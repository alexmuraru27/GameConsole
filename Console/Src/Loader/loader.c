#include "loader.h"
#include "logger.h"
#include "sd_layout.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The single open-game file handle + its directory entry. Internal to the loader;
 * reached from outside only through loaderGetFile()/loaderGetFileInfo(). */
static FIL s_active_file;
static FILINFO s_active_fileinfo;
static bool s_is_file_opened = false;

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

bool loaderIsFileOpened(void)
{
    return s_is_file_opened;
}

/* Walk Games/, skipping hidden/system/sub-directory entries, and return the
 * FILINFO of the `index`-th .bin file (0-based) in *out. FR_OK if found; FR_NO_FILE
 * if there are fewer than index+1 binaries; or the FatFs error that ended the walk.
 * The one place the "scan Games/, filter .bin, act on the Nth match" walk lives. */
static FRESULT findNthBinary(uint32_t index, FILINFO *out)
{
    DIR dir;
    FILINFO finfo;
    FRESULT res = f_opendir(&dir, SD_DIR_GAMES);
    if (res != FR_OK)
    {
        return res;
    }

    uint32_t match = 0U;
    for (;;)
    {
        res = f_readdir(&dir, &finfo);
        if (res != FR_OK)
        {
            break;
        }
        if (finfo.fname[0U] == 0U)
        {
            res = FR_NO_FILE;
            break;
        }
        if (finfo.fattrib & (AM_HID | AM_SYS | AM_DIR))
        {
            continue;
        }
        if (isBinaryFile(finfo.fname))
        {
            if (match == index)
            {
                *out = finfo;
                res = FR_OK;
                break;
            }
            match++;
        }
    }

    f_closedir(&dir);
    return res;
}

uint32_t loaderGetBinaryFilesNumberInDirectory(void)
{
    DIR dir;
    FILINFO finfo;
    if (f_opendir(&dir, SD_DIR_GAMES) != FR_OK)
    {
        return 0U;
    }

    uint32_t file_count = 0U;
    while (f_readdir(&dir, &finfo) == FR_OK && finfo.fname[0U] != 0U)
    {
        if ((finfo.fattrib & (AM_HID | AM_SYS | AM_DIR)) == 0U && isBinaryFile(finfo.fname))
        {
            file_count++;
        }
    }

    f_closedir(&dir);
    return file_count;
}

FIL *loaderGetFile(void)
{
    if (s_is_file_opened)
    {
        return &s_active_file;
    }
    return NULL;
}

FILINFO *loaderGetFileInfo(void)
{
    if (s_is_file_opened)
    {
        return &s_active_fileinfo;
    }
    return NULL;
}

FRESULT loaderCloseFile(void)
{
    s_is_file_opened = false;
    return f_close(&s_active_file);
}

FRESULT loaderOpenFile(const uint32_t binary_index)
{
    FILINFO finfo;
    FRESULT res = findNthBinary(binary_index, &finfo);
    if (res != FR_OK)
    {
        return res;
    }

    /* f_readdir gives the bare name; open it under Games/. */
    char path[FF_LFN_BUF + sizeof(SD_DIR_GAMES) + 1U];
    snprintf(path, sizeof(path), "%s/%s", SD_DIR_GAMES, finfo.fname);
    res = f_open(&s_active_file, path, FA_READ);
    s_active_fileinfo = finfo;
    if (res == FR_OK)
    {
        s_is_file_opened = true;
    }
    return res;
}

FRESULT loaderGetFilenameByIndex(const uint32_t binary_index, char *const filename_out, uint32_t *const filename_length)
{
    if (!filename_out || !filename_length)
    {
        return FR_INVALID_PARAMETER;
    }

    // Clear output buffer
    filename_out[0] = '\0';
    *filename_length = 0U;

    FILINFO finfo;
    FRESULT res = findNthBinary(binary_index, &finfo);
    if (res != FR_OK)
    {
        return res;
    }

    strncpy(filename_out, finfo.fname, FF_LFN_BUF - 1U);
    filename_out[FF_LFN_BUF - 1U] = '\0';
    *filename_length = (uint32_t)strlen(finfo.fname);
    return FR_OK;
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

