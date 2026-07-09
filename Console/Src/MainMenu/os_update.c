#include "os_update.h"

#include <stdio.h>

#include "menu_common.h"
#include "logger.h"
#include "loader.h"
#include "flash_ui.h"
#include "os_flasher.h"
#include "sd_layout.h"
#include "ff.h"

/* The OS image is looked up under Firmware/ (where Poll Updates places an "os"
 * category download). */
#define OS_IMAGE_PATH SD_DIR_FIRMWARE "/" CONSOLE_FIRMWARE_FILENAME

#define UI_TITLE "OS UPDATE"

/* osFlasherStage progress callback: redraw the bar each block. */
static void onProgress(uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    flashUiScreen(UI_TITLE, "Writing image...", g_menu_pal_item_sel, true, done, total, "do not power off");
}

void osUpdateRun(void)
{
    menuResetSurface();
    LOGGER_LOG_INFO(LOGGER_FLASHER, "OS update requested");

    /* Need the card mounted and the image present. */
    FILINFO info;
    if (!loaderMediaPresent() || f_stat(OS_IMAGE_PATH, &info) != FR_OK)
    {
        LOGGER_LOG_WARN(LOGGER_FLASHER, "'%s' not found on SD", OS_IMAGE_PATH);
        flashUiFail(UI_TITLE, "Console.bin not found on SD");
        return;
    }

    /* Stream the image into staging and verify it by readback. Erasing staging
     * takes a couple of seconds with no progress callback yet, so show a holding
     * line first. The running OS is untouched throughout — only staging is written,
     * so a card yank or power loss here leaves the current OS intact. */
    flashUiScreen(UI_TITLE, "Preparing flash...", g_menu_pal_item_sel, false, 0U, 0U, "do not power off");

    uint32_t staged_crc = 0U;
    uint32_t image_size = 0U;
    const OsFlashStatus status = osFlasherStage(OS_IMAGE_PATH, &staged_crc, &image_size, onProgress, NULL);
    if (status != OS_FLASH_OK)
    {
        char line[40];
        snprintf(line, sizeof(line), "Failed: %s", osFlasherStatusString(status));
        flashUiFail(UI_TITLE, line);
        return;
    }

    /* Confirm the staged image is the intended one before the irreversible step.
     * On cancel, staging is left written but uncommitted (no valid header), so the
     * bootloader ignores it and the current OS keeps running. */
    if (!flashUiPreflashConfirm(UI_TITLE, CONSOLE_FIRMWARE_FILENAME, staged_crc))
    {
        return;
    }

    /* Commit + reboot. The bootloader applies staging into the app region, verifies
     * it by readback CRC, and only then runs it — re-applying if interrupted. */
    flashUiScreen(UI_TITLE, "Committing - rebooting", g_menu_pal_accent, false, 0U, 0U, NULL);
    const OsFlashStatus commit = osFlasherCommitAndReboot(image_size, staged_crc);

    /* Only reached if the header write failed (commit normally reboots). */
    char line[40];
    snprintf(line, sizeof(line), "Failed: %s", osFlasherStatusString(commit));
    flashUiFail(UI_TITLE, line);
}
