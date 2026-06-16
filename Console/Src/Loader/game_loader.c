#include "game_loader.h"
#include "ff.h"
#include "loader.h"
#include "asset_loader.h"
#include "logger.h"
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

/* Derive the game's asset pak from its .bin name (GameXO.bin -> GameXO.pak) and
 * bind it so the asset loader can serve this game's assets. The pak is optional:
 * a game shipping no assets just has none, which the asset loader treats as OK. */
static void bindGamePak(void)
{
    const FILINFO *finfo = loaderGetFileInfo();
    if (finfo == NULL)
    {
        return;
    }

    char pak_name[FF_LFN_BUF];
    strncpy(pak_name, finfo->fname, sizeof(pak_name) - 1U);
    pak_name[sizeof(pak_name) - 1U] = '\0';

    char *ext = strrchr(pak_name, '.');
    if (ext == NULL)
    {
        return;
    }
    strcpy(ext, ".pak"); /* ".pak" is the same length as ".bin", so this fits in place */

    assetLoaderOpenPak(pak_name);
}

uint8_t gameLoaderLoadGame(uint8_t binary_index)
{
    FRESULT res;
    s_is_game_header_valid = false;

    LOGGER_LOG_INFO(LOGGER_LOADER, "loading game index %u", binary_index);

    res = loaderOpenFile(binary_index);
    if (res != FR_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "open game %u failed (%d)", binary_index, res);
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
            res = loadRegionToMemory(loaderGetFile(), s_game_header.text_start, s_game_header.header_start, text_file_size);
            if (res != FR_OK)
            {
                return res;
            }
        }

        if (ro_data_file_size > 0U)
        {
            res = loadRegionToMemory(loaderGetFile(), s_game_header.ro_data_start, s_game_header.header_start, ro_data_file_size);
            if (res != FR_OK)
            {
                return res;
            }
        }

        if (data_file_size > 0U)
        {
            res = loadRegionToMemory(loaderGetFile(), s_game_header.data_start, s_game_header.header_start, data_file_size);
            if (res != FR_OK)
            {
                return res;
            }
        }

        // Bind the game's asset pak before handing over control, so asset
        // requests during gameplay resolve against this game's .pak.
        bindGamePak();

        LOGGER_LOG_INFO(LOGGER_LOADER, "starting game @ 0x%08lX", (unsigned long)s_game_header.entry_point);

        // Set PSP to the game's stack and switch Thread mode to use PSP.
        // This isolates the game's stack from the console: if the game faults,
        // the CPU enters Handler mode on MSP (console stack), leaving the
        // game's PSP stack intact for debugging.
        __asm volatile("msr psp, %0" : : "r"(s_game_header.stack_top));

        uint32_t control;
        __asm volatile("mrs %0, control" : "=r"(control));
        control |= 2U; // CONTROL[1] = SPSEL → use PSP in Thread mode
        __asm volatile("msr control, %0" : : "r"(control));
        __asm volatile("isb" : : : "memory");

        void (*game_entry)(void) = (void (*)(void))s_game_header.entry_point;
        game_entry();

        // Switch Thread mode back to MSP before touching console state
        __asm volatile("mrs %0, control" : "=r"(control));
        control &= ~2U; // CONTROL[1] = 0 → back to MSP
        __asm volatile("msr control, %0" : : "r"(control));
        __asm volatile("isb" : : : "memory");

        LOGGER_LOG_INFO(LOGGER_LOADER, "game returned to console");

        assetLoaderClosePak();
        loaderCloseFile();
    }

    return 0U;
}

uint8_t gameLoaderCloseGame()
{
    return loaderCloseFile();
}