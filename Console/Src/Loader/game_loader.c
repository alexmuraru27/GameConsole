#include "game_loader.h"
#include "ff.h"
#include "loader.h"
#include "asset_loader.h"
#include "settings_storage.h"
#include "logger.h"
#include "sysclock.h"
#include "scheduler.h"
#include "sd_layout.h"
#include <stdio.h>
#include <string.h>

/* GAME_RAM bounds (from common.ld) — the flat game image is copied here. */
extern uint32_t __game_ram_start, __game_ram_size;
#define GAME_RAM_BASE ((uint32_t)&__game_ram_start)
#define GAME_RAM_LEN ((uint32_t)&__game_ram_size)

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

/* Copy the whole .bin (header + .text + .rodata + .data — all linked at their
 * final GAME_RAM addresses) to GAME_RAM_BASE. .bss and the CCM asset arena are
 * NOLOAD (not in the file); the game zeroes its own .bss in _game_start. */
static FRESULT loadImageToGameRam(FIL *file)
{
    const uint32_t total = f_size(file);
    if (total == 0U || total > GAME_RAM_LEN)
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "game image %lu B does not fit GAME_RAM (%lu B)",
                         (unsigned long)total, (unsigned long)GAME_RAM_LEN);
        return FR_INT_ERR;
    }

    FRESULT res = f_lseek(file, 0U);
    if (res != FR_OK)
    {
        return res;
    }

    UINT bytes_read = 0U;
    res = f_read(file, (void *)GAME_RAM_BASE, total, &bytes_read);
    if (res != FR_OK || bytes_read != total)
    {
        return (res != FR_OK) ? res : FR_INT_ERR;
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

    /* The pak lives beside the .bin under Games/. */
    char pak_path[FF_LFN_BUF + sizeof(SD_DIR_GAMES) + 1U];
    snprintf(pak_path, sizeof(pak_path), "%s/%s", SD_DIR_GAMES, pak_name);
    assetLoaderOpenPak(pak_path);
}

/* Bind a save slot keyed by the game's .bin name. Binding is cheap — the actual
 * 2 KB slot is only allocated on the game's first settingsWrite — so every game
 * gets a slot lazily and no per-game opt-in flag is needed. Dropped on return. */
static void bindGameSettings(void)
{
    const FILINFO *finfo = loaderGetFileInfo();
    if (finfo == NULL)
    {
        return;
    }

    settingsStorageBindGame(finfo->fname);
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

    UINT header_bytes_read = 0U;
    res = f_read(loaderGetFile(), &s_game_header, sizeof(GameBinaryHeader), &header_bytes_read);
    if (res != FR_OK || header_bytes_read != sizeof(GameBinaryHeader))
    {
        loaderCloseFile();
        return (res != FR_OK) ? res : (uint8_t)FR_INT_ERR;
    }

    if (s_game_header.magic != GAME_BINARY_MAGIC)
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "bad game magic 0x%08lX", (unsigned long)s_game_header.magic);
        loaderCloseFile();
        return GAME_LOADER_RET_ERR;
    }
    if (s_game_header.abi_version != CONSOLE_ABI_VERSION)
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "game ABI v%lu != console ABI v%u",
                         (unsigned long)s_game_header.abi_version, (unsigned)CONSOLE_ABI_VERSION);
        loaderCloseFile();
        return GAME_LOADER_RET_ERR;
    }
    s_is_game_header_valid = true;

    res = loadImageToGameRam(loaderGetFile());
    if (res != FR_OK)
    {
        loaderCloseFile();
        return res;
    }

    /* Bind the game's asset pak and settings slot before handing over control. */
    bindGamePak();
    bindGameSettings();

    LOGGER_LOG_INFO(LOGGER_LOADER, "starting game @ 0x%08lX", (unsigned long)s_game_header.entry_point);

    /* Hand over to the kernel: it builds the game's unprivileged context, enables
     * the MPU confinement, and switches in. This returns only when the game exits
     * cleanly or crashes — either way the console resumes privileged on the MSP. */
    kernelRunGame(s_game_header.entry_point);

    const bool crashed = kernelGameCrashed();
    if (crashed)
    {
        LOGGER_LOG_ERROR(LOGGER_LOADER, "game crashed; recovered to console");
    }
    else
    {
        LOGGER_LOG_INFO(LOGGER_LOADER, "game returned to console");
    }

    settingsStorageUnbindGame();
    assetLoaderClosePak();
    loaderCloseFile();
    return crashed ? GAME_LOADER_RET_CRASHED : GAME_LOADER_RET_OK;
}

uint8_t gameLoaderCloseGame()
{
    return loaderCloseFile();
}