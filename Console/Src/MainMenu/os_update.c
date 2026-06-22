#include "os_update.h"

#include <stdio.h>

#include "menu_common.h"
#include "buzzer.h"
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

static const uint16_t s_fail_notes[] = {NOTE_A4, 120U, NOTE_E4, 200U};

/* osFlasherStage progress callback: redraw the bar each block. */
static void onProgress(uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    flashUiScreen(UI_TITLE, "Writing image...", g_menu_pal_item_sel, true, done, total, "do not power off");
}

/* Compare the just-staged image's CRC to the one recorded at download time and let
 * the user decide on a mismatch. The image can legitimately differ (hand-copied,
 * or a stale record), but a flaky-SD corruption looks the same — so we warn rather
 * than hard-block. Returns true if the commit should proceed. */
static bool stagedImageOk(uint32_t staged_crc)
{
    uint32_t want_crc = 0U;
    if (!flashUiRecordedCrc(CONSOLE_FIRMWARE_FILENAME, &want_crc))
    {
        /* No download record (e.g. the image was copied onto the card by hand): no
         * reference CRC. Proceed — the bootloader still readback-verifies the apply
         * against the committed CRC — but flag the skipped pre-check. */
        LOGGER_LOG_WARN(LOGGER_FLASHER, "no CRC record for '%s'; committing without pre-verify", OS_IMAGE_PATH);
        return true;
    }
    if (staged_crc != want_crc)
    {
        LOGGER_LOG_WARN(LOGGER_FLASHER, "staged CRC mismatch on '%s': have %08lX want %08lX (asking user)",
                        OS_IMAGE_PATH, (unsigned long)staged_crc, (unsigned long)want_crc);
        const bool proceed = flashUiConfirmMismatch(UI_TITLE, staged_crc, want_crc);
        LOGGER_LOG_INFO(LOGGER_FLASHER, "user chose to %s", proceed ? "flash anyway" : "cancel");
        return proceed;
    }
    LOGGER_LOG_INFO(LOGGER_FLASHER, "staged CRC ok (%08lX)", (unsigned long)want_crc);
    return true;
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
        buzzerPlay(0U, false, s_fail_notes, 2U);
        flashUiWaitBack(UI_TITLE, "Console.bin not found on SD", g_menu_pal_alert);
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
        buzzerPlay(0U, false, s_fail_notes, 2U);
        flashUiWaitBack(UI_TITLE, line, g_menu_pal_alert);
        return;
    }

    /* Confirm the staged image is the intended one before the irreversible step. */
    if (!stagedImageOk(staged_crc))
    {
        /* Staging is left written but uncommitted (no valid header), so the
         * bootloader ignores it and the current OS keeps running. */
        return;
    }

    /* Commit + reboot. The bootloader applies staging into the app region, verifies
     * it by readback CRC, and only then runs it — re-applying if interrupted. */
    flashUiScreen(UI_TITLE, "Committing - rebooting", g_menu_pal_accent, false, 0U, 0U, NULL);
    const OsFlashStatus commit = osFlasherCommitAndReboot(image_size, staged_crc);

    /* Only reached if the header write failed (commit normally reboots). */
    char line[40];
    snprintf(line, sizeof(line), "Failed: %s", osFlasherStatusString(commit));
    buzzerPlay(0U, false, s_fail_notes, 2U);
    flashUiWaitBack(UI_TITLE, line, g_menu_pal_alert);
}
