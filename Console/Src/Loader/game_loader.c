#include "game_loader.h"
#include "ff.h"
#include "loader.h"
#include "sysclock.h"
#include <string.h>

GameBinaryHeader s_game_header;
bool s_is_game_header_valid = false;

uint8_t gameLoaderGetHeader(GameBinaryHeader *const game_header)
{
    if (!s_is_game_header_valid)
    {
        return GAME_LOADER_RET_ERR;
    }
    memcpy(game_header, &s_game_header, sizeof(GameBinaryHeader));
    return GAME_LOADER_RET_OK;
}

static void clearBss(const uint32_t start_addr, const uint32_t size)
{
    memset((void *)start_addr, 0U, size);
}

static uint8_t loadRegionToMemory(FIL *file, const uint32_t region_addr_start, const uint32_t header_addr_start, const uint32_t region_size)
{
    FRESULT res;
    UINT bytes_read;
    uint8_t buffer[128U];
    uint32_t remaining_bytes = region_size;
    uint32_t dest_addr = region_addr_start;

    // file offset: region memory address - header start address
    const uint32_t file_offset = region_addr_start - header_addr_start;

    res = f_lseek(file, file_offset);
    if (res != FR_OK)
    {
        return res;
    }

    while (remaining_bytes > 0)
    {
        uint32_t bytes_to_read = (remaining_bytes > sizeof(buffer)) ? sizeof(buffer) : remaining_bytes;
        res = f_read(file, buffer, bytes_to_read, &bytes_read);
        if (res != FR_OK || bytes_read == 0)
        {
            break;
        }

        memcpy((void *)dest_addr, buffer, bytes_read);

        remaining_bytes -= bytes_read;
        dest_addr += bytes_read;

        // read less than requested, end of file
        if (bytes_read < bytes_to_read)
        {
            break;
        }
    }

    if (remaining_bytes > 0)
    {
        return FR_INT_ERR;
    }

    return FR_OK;
}
uint8_t gameLoaderLoadGame(uint8_t binary_index)
{
    FRESULT res;
    s_is_game_header_valid = false;
    res = loaderOpenFile(binary_index);
    if (res != FR_OK)
    {
        return res;
    }

    uint8_t game_header_buffer[sizeof(GameBinaryHeader)];
    UINT game_header_buffer_bytes_read;
    res = f_read(loaderGetFile(), game_header_buffer, sizeof(GameBinaryHeader), &game_header_buffer_bytes_read);
    if (res != FR_OK || game_header_buffer_bytes_read == 0)
    {
        return res;
    }
    else
    {
        memcpy(&s_game_header, &game_header_buffer, sizeof(GameBinaryHeader));
        s_is_game_header_valid = true;
        uint32_t text_file_size = s_game_header.text_end - s_game_header.text_start;
        uint32_t ro_data_file_size = s_game_header.ro_data_end - s_game_header.ro_data_start;
        uint32_t data_file_size = s_game_header.data_end - s_game_header.data_start;
        uint32_t bss_file_size = s_game_header.bss_end - s_game_header.bss_start;

        clearBss(s_game_header.bss_start, bss_file_size);

        if (text_file_size > 0U)
        {
            loadRegionToMemory(loaderGetFile(), s_game_header.text_start, s_game_header.header_start, text_file_size);
        }

        if (ro_data_file_size > 0U)
        {
            loadRegionToMemory(loaderGetFile(), s_game_header.ro_data_start, s_game_header.header_start, ro_data_file_size);
        }

        if (data_file_size > 0U)
        {
            loadRegionToMemory(loaderGetFile(), s_game_header.data_start, s_game_header.header_start, data_file_size);
        }

        // TODO - switch the stack pointer
        // TODO - save the OS stack pointer first
        // __asm volatile("msr msp, %0" ::"r"(game_header->data_end) :);
        void (*game_entry)(void) = (void (*)(void))s_game_header.entry_point;
        game_entry();
        loaderCloseFile();

        // TODO restore OS stack pointer after game return
    }

    return 0U;
}

uint8_t gameLoaderCloseGame()
{
    return loaderCloseFile();
}