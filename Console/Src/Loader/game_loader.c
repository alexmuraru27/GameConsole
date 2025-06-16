#include "game_loader.h"
#include "ff.h"
#include "loader.h"
#include "sysclock.h"
#include "string.h"

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
    FIL file;
    FRESULT res;

    res = loaderOpenBinaryFileByIndex(binary_index, &file);
    if (res != FR_OK)
    {
        return res;
    }

    uint8_t game_header_buffer[sizeof(GameHeader)];
    UINT game_header_buffer_bytes_read;
    res = f_read(&file, game_header_buffer, sizeof(GameHeader), &game_header_buffer_bytes_read);
    if (res != FR_OK || game_header_buffer_bytes_read == 0)
    {
        return res;
    }
    else
    {
        GameHeader *game_header_from_bin = (GameHeader *)&game_header_buffer;
        uint32_t text_file_size = game_header_from_bin->text_end - game_header_from_bin->text_start;
        uint32_t ro_data_file_size = game_header_from_bin->ro_data_end - game_header_from_bin->ro_data_start;
        uint32_t data_file_size = game_header_from_bin->data_end - game_header_from_bin->data_start;
        uint32_t bss_file_size = game_header_from_bin->bss_end - game_header_from_bin->bss_start;

        clearBss(game_header_from_bin->bss_start, bss_file_size);

        if (text_file_size > 0U)
        {
            loadRegionToMemory(&file, game_header_from_bin->text_start, game_header_from_bin->header_start, text_file_size);
        }

        if (ro_data_file_size > 0U)
        {
            loadRegionToMemory(&file, game_header_from_bin->ro_data_start, game_header_from_bin->header_start, ro_data_file_size);
        }

        if (data_file_size > 0U)
        {
            loadRegionToMemory(&file, game_header_from_bin->data_start, game_header_from_bin->header_start, data_file_size);
        }

        // __asm volatile("msr msp, %0" ::"r"(game_header->data_end) :);
        // void (*game_entry)(void) = (void (*)(void))game_header_from_bin->entry_point;
        // game_entry();
    }

    return 0U;
}