#include "Loader/loader_media.h"
#include <stm32f407xx.h>
#include "Peripherals/systime.h"
#include "diskio_integration.h" /* diskMarkUninitialized */
#include "Peripherals/sysclock.h"           /* getSysTime */
#include "Logger/logger.h"
#include "sd_layout.h"
#include "ff.h"
#include <stddef.h> /* NULL */

/* Mounted FatFs work area for the SD volume, plus the debounced card-detect
 * state. The card-detect line can bounce while a card is being seated, so a
 * presence change must hold steady for MEDIA_DEBOUNCE_MS before we act on it. */
static FATFS s_fatfs;
static bool s_media_present;   /* committed (mounted) presence */
static bool s_media_candidate; /* raw reading awaiting debounce */
static uint32_t s_media_since; /* when the candidate first appeared */
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
        diskMarkUninitialized();     /* re-run SDIO init for the new card */
        f_mount(&s_fatfs, "0:", 1U); /* remount and read its filesystem */
        ensureDirs();                /* the new card may lack the layout */
        LOGGER_LOG_INFO(LOGGER_LOADER, "SD card inserted");
    }
    else
    {
        f_mount(NULL, "0:", 0U); /* drop the stale mount */
        diskMarkUninitialized();
        LOGGER_LOG_INFO(LOGGER_LOADER, "SD card removed");
    }
    return true;
}

bool loaderMediaPresent(void)
{
    return s_media_present;
}
