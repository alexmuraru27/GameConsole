#include "game_console_api.h"
#include "sysclock.h"
#include "test_api.h"
#include "usart.h"
#include "ff.h"

extern uint32_t __game_header_start;
extern ConsoleAPIHeader __game_console_api_start; // linker
static void testApiCalls()
{
    ConsoleAPIHeader *api_hdr_ptr = (ConsoleAPIHeader *)&__game_console_api_start;
    if (api_hdr_ptr->magic == API_MAGIC || api_hdr_ptr->version == API_VERSION)
    {
        api_hdr_ptr->api.debugString("Hello from shared api :D\r\n");
    }
}

static void testGameHeader(void)
{
    GameHeader *game_header = (GameHeader *)&__game_header_start;

    if (game_header->magic != 0x47414D45U)
    {
        return;
    }

    uint32_t header_file_size = 0U;
    uint32_t text_file_size = 0U;
    uint32_t ro_data_file_size = 0U;
    uint32_t assets_file_size = 0U;
    uint32_t data_file_size = 0U;
    uint32_t bss_file_size = 0U;
    uint32_t total_file_size = 0U;

    uint32_t header_size = 0U;
    uint32_t text_size = 0U;
    uint32_t ro_data_size = 0U;
    uint32_t assets_size = 0U;
    uint32_t data_size = 0U;
    uint32_t bss_size = 0U;
    uint32_t total_size = 0U;
    DIR dir;
    FIL file;
    FRESULT res;
    FILINFO finfo;
    res = f_opendir(&dir, "0:");
    if (res == FR_OK)
    {
        while (1)
        {
            res = f_readdir(&dir, &finfo);

            if (res != FR_OK || finfo.fname[0] == 0)
            {
                break;
            }

            if (finfo.fattrib & (AM_HID | AM_SYS))
            {
                continue;
            }

            if (!(finfo.fattrib & AM_DIR))
            {
                res = f_open(&file, finfo.fname, FA_READ);

                if (res != FR_OK)
                {
                    debugString("Open fail");
                    return;
                }

                uint8_t buffer[sizeof(GameHeader)];
                UINT bytesRead;
                res = f_read(&file, buffer, sizeof(GameHeader), &bytesRead);
                if (res != FR_OK || bytesRead == 0)
                {
                    break;
                }
                else
                {
                    debugString("\r\n-BIN file");
                    GameHeader *game_header_from_bil = (GameHeader *)&buffer;

                    header_file_size = game_header_from_bil->header_end - game_header_from_bil->header_start;
                    text_file_size = game_header_from_bil->text_end - game_header_from_bil->text_start;
                    ro_data_file_size = game_header_from_bil->ro_data_end - game_header_from_bil->ro_data_start;
                    assets_file_size = game_header_from_bil->assets_end - game_header_from_bil->assets_start;
                    data_file_size = game_header_from_bil->data_end - game_header_from_bil->data_start;
                    bss_file_size = game_header_from_bil->bss_end - game_header_from_bil->bss_start;
                    total_file_size = header_file_size + text_file_size + ro_data_file_size + assets_file_size + data_file_size + bss_file_size;
                    debugString("\r\nheader_file_size = ");
                    debugInt(header_file_size);
                    debugString("\r\ntext_file_size = ");
                    debugInt(text_file_size);
                    debugString("\r\nro_data_file_size = ");
                    debugInt(ro_data_file_size);
                    debugString("\r\ndata_file_size = ");
                    debugInt(data_file_size);
                    debugString("\r\nbss_file_size = ");
                    debugInt(bss_file_size);
                    debugString("\r\nassets_file_size = ");
                    debugInt(assets_file_size);
                    debugString("\r\ntotal_file_size = ");
                    debugInt(total_file_size);

                    break;
                }
            }
        }
    }

    f_closedir(&dir);
    debugString("\r\n\r\n-In memory game file");
    header_size = game_header->header_end - game_header->header_start;
    text_size = game_header->text_end - game_header->text_start;
    ro_data_size = game_header->ro_data_end - game_header->ro_data_start;
    assets_size = game_header->assets_end - game_header->assets_start;
    data_size = game_header->data_end - game_header->data_start;
    bss_size = game_header->bss_end - game_header->bss_start;
    total_size = header_size + text_size + ro_data_size + assets_size + data_size + bss_size;
    debugString("\r\nheader_size = ");
    debugInt(header_size);
    debugString("\r\ntext_size = ");
    debugInt(text_size);
    debugString("\r\nro_data_size = ");
    debugInt(ro_data_size);
    debugString("\r\ndata_size = ");
    debugInt(data_size);
    debugString("\r\nbss_size = ");
    debugInt(bss_size);
    debugString("\r\nassets_size = ");
    debugInt(assets_size);
    debugString("\r\ntotal_size = ");
    debugInt(total_size);

    // __asm volatile("msr msp, %0" ::"r"(game_header->data_end) :);
    // void (*game_entry)(void) = (void (*)(void))game_header->entry_point;
    // game_entry();
}

void testApi(void)
{
    debugString("\r\n====== API Test Start ======\r\n");
    debugString("\r\n=== API ===\r\n");
    testApiCalls();
    debugString("\r\n=== Header ===\r\n");
    testGameHeader();
    debugString("\r\n====== API Test End ======r\n");
}