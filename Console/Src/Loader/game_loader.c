#include "game_loader.h"
#include "ff.h"
#include "loader.h"
#include "asset_loader.h"
#include "settings_storage.h"
#include "logger.h"
#include "sysclock.h"
#include "scheduler.h"
#include "watchdog.h"
#include "mp_session.h"
#include "sd_layout.h"
#include <stm32f407xx.h> /* DWT->CYCCNT for the frame delta */
#include <stdio.h>
#include <string.h>

/* GAME_RAM bounds (from common.ld) — the flat game image is copied here. */
extern uint32_t __game_ram_start, __game_ram_size;
#define GAME_RAM_BASE ((uint32_t)&__game_ram_start)
#define GAME_RAM_LEN ((uint32_t)&__game_ram_size)

/* Frame delta — the wall-clock time between successive update() calls, exposed to
 * games as getDeltaTimeUs() for frame-rate-independent movement/animation. It is
 * measured in gameRuntimeCollect (run once per frame, just before update) from
 * DWT->CYCCNT, the free-running 168 MHz cycle counter enabled in swoInit: ~6 ns
 * resolution at zero interrupt cost. The first frame of a session reports 0 (no
 * prior frame), and the delta is clamped so a one-off stall (a long SD access,
 * say) can't fling a game's sprites across the screen in a single step. */
#define CPU_CYCLES_PER_US 168U      /* DWT->CYCCNT runs at the 168 MHz CPU clock */
#define FRAME_DELTA_MAX_US 100000U  /* 100 ms safety cap */
static uint32_t s_prev_frame_cyc;
static bool s_frame_timing_started;
static volatile uint32_t s_frame_delta_us;

uint32_t gameLoaderGetDeltaUs(void)
{
    return s_frame_delta_us;
}

/* Frame-START seam (before update), privileged, from inside kernelRunGame's loop.
 * Measures the frame delta (above) and, while a multiplayer session is up, ingests
 * the reply to the PREVIOUS frame's ESP-NOW request — which streamed into the RX
 * DMA ring during the last render(), so this rarely waits. The game's update()
 * then reads the freshly-filled inbox via the non-blocking mp* syscalls. The OS
 * does NOT cap the frame rate — a game runs uncapped and paces itself with delay()
 * if it wants a fixed rate (the renderer test runs flat out to measure raw
 * throughput; GameXO sleeps out each frame to hold ~60 FPS). */
static void gameRuntimeCollect(void)
{
    const uint32_t now = DWT->CYCCNT;
    if (!s_frame_timing_started)
    {
        s_frame_timing_started = true; /* first frame: no elapsed time yet */
        s_frame_delta_us = 0U;
    }
    else
    {
        const uint32_t us = (now - s_prev_frame_cyc) / CPU_CYCLES_PER_US; /* wrap-safe */
        s_frame_delta_us = (us > FRAME_DELTA_MAX_US) ? FRAME_DELTA_MAX_US : us;
    }
    s_prev_frame_cyc = now;

    if (mpSessionActive())
    {
        mpSessionCollect(getSysTime());
    }
}

/* Frame-END seam (after update, before render), privileged. While a session is up,
 * assemble this frame's outbound batch (the messages update() just queued, plus
 * due beacons/heartbeats) and arm it over the async TX — the round-trip then
 * overlaps render() instead of busy-waiting the CPU, which is the whole point of
 * the split. No frame pacing here by design; the game self-paces with delay(). */
static void gameRuntimeFlush(void)
{
    if (mpSessionActive())
    {
        mpSessionFlush(getSysTime());
    }
}

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
    s_frame_timing_started = false; /* fresh frame-delta baseline for this game */

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

    /* Reset the watchdog window before handing off: the image read above ran since
     * the last menu kick, and the kernel's bootstrap + init run before its loop
     * starts kicking per frame. */
    watchdogKick();

    /* Hand over to the kernel: it programs the MPU confinement, runs the game's
     * bootstrap + init, then loops {collect; update; send; render} uncapped (the
     * game paces itself). collect ingests inbound before update, send arms outbound
     * after it so the ESP-NOW round-trip overlaps render. Returns only when the game
     * exits cleanly or crashes — either way the console resumes privileged on the MSP. */
    kernelRunGame(&s_game_header, gameRuntimeCollect, gameRuntimeFlush);

    /* A game may have opened a multiplayer session; tear it down unconditionally so
     * a clean exit OR a crash can never leave the ESP-NOW link or a peer session up
     * for the next game. Idempotent when no session was active. */
    mpSessionStop();

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