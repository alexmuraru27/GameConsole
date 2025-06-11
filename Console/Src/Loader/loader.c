#include "loader.h"
#include "usart.h"
#include "stdbool.h"
#include "stdlib.h"
#include "string.h"

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

uint32_t loaderGetBinaryFilesNumberInDirectory(void)
{
    FRESULT res;
    DIR dir;
    FILINFO finfo;
    uint32_t file_count = 0U;

    res = f_opendir(&dir, "0:");
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

FRESULT loaderOpenBinaryFileByIndex(const uint32_t index, FIL *const file)
{
    FRESULT res;
    DIR dir;
    FILINFO finfo;
    uint32_t binary_file_index = 0U;

    if (!file)
    {
        return FR_INVALID_PARAMETER;
    }

    res = f_opendir(&dir, "0:");
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
            if (binary_file_index == index)
            {
                res = f_open(file, finfo.fname, FA_READ);
                f_closedir(&dir);
                return res;
            }
            binary_file_index++;
        }
    }
}

FRESULT loaderGetBinaryFilenameByIndex(const uint32_t index, char *const filename_out, uint32_t *const filename_length)
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

    res = f_opendir(&dir, "0:");
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
            if (binary_file_index == index)
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