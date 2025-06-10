#include "test_fatfs.h"
#include "usart.h"
#include "ff.h"
#include "sysclock.h"

static FATFS s_fatfs;

void fatfsDirRead(void)
{
    FRESULT res;
    DIR dir;
    FILINFO finfo;
    uint32_t file_count = 0;
    uint32_t total_size = 0;
    const char *path = "0:";

    debugString("=== FatFs Dir Read Test ===\r\n");

    // Initialize FatFs
    res = f_mount(&s_fatfs, "0:", 1); // Mount immediately

    if (res != FR_OK)
    {
        debugString("Mount fail:");
        debugInt(res);
        debugString("\r\n");
        return;
    }

    res = f_opendir(&dir, path);
    if (res == FR_OK)
    {
        while (1)
        {
            res = f_readdir(&dir, &finfo);

            if (res != FR_OK || finfo.fname[0] == 0)
            {
                break;
            }

            // Skip hidden/system files and directories (like SYSTEM~1)
            if (finfo.fattrib & (AM_HID | AM_SYS))
            {
                continue;
            }

            file_count++;

            // Print file/directory info
            if (finfo.fattrib & AM_DIR)
            {
                debugString("[D] ");
            }
            else
            {
                debugString("[F] ");
                total_size += finfo.fsize;
            }

            debugString(finfo.fname);

            if (!(finfo.fattrib & AM_DIR))
            {
                debugString(" ");
                debugInt(finfo.fsize);
            }
            delay(1);
            debugString("\r\n");
        }
        f_closedir(&dir);

        debugString("Files:");
        debugInt(file_count);
        debugString(" Size:");
        debugInt(total_size);
        debugString("\r\n");

        if (file_count == 0)
        {
            debugString("Empty dir\r\n");
        }
    }
    else
    {
        debugString("Dir fail:");
        debugInt(res);
        debugString("\r\n");
    }

    debugString("Done\r\n");
}

uint8_t fatfsReadFile(const char *filename, uint32_t maxBytes)
{
    FIL file;
    FRESULT res;
    UINT bytesRead;
    uint8_t buffer[64]; // Small buffer for hex display
    uint32_t totalRead = 0;

    debugString("=== FatFs Dir Read Test ===\r\n");
    debugString("Reading binary: ");
    debugString(filename);
    debugString("\r\n");

    // Open file for reading
    res = f_open(&file, filename, FA_READ);
    if (res != FR_OK)
    {
        debugString("Open fail:");
        debugHex(res);
        debugString("\r\n");
        return 1;
    }

    debugString("File size:");
    debugHex(f_size(&file));
    debugString(" bytes\r\n");
    debugString("Hex content (first ");
    debugHex(maxBytes);
    debugString(" bytes):\r\n");

    // Read and display file in chunks
    while (totalRead < maxBytes)
    {
        uint32_t toRead = (maxBytes - totalRead > sizeof(buffer)) ? sizeof(buffer) : (maxBytes - totalRead);

        res = f_read(&file, buffer, toRead, &bytesRead);
        if (res != FR_OK || bytesRead == 0)
        {
            break;
        }

        for (uint32_t i = 0; i < bytesRead; i++)
        {
            if ((totalRead + i) % 16 == 0)
            {
                debugString("\r\n");
                debugHex(totalRead + i);
                debugString(": ");
            }
            debugChar(buffer[i]);
            debugString(" ");
            delay(1);
        }

        totalRead += bytesRead;
    }

    debugString("\r\nTotal read:");
    debugHex(totalRead);
    debugString(" bytes\r\n");
    delay(1);
    usartBufferFlush();
    f_close(&file);
    return 0;
}

void testFatFs(void)
{
    delay(10);
    debugString("====== FatFs Test ======\r\n");
    delay(10);
    fatfsDirRead();
    fatfsReadFile("test.txt", 100U);

    delay(10);
    debugString("====== FatFs Test End ======\r\n");
    delay(10);
}