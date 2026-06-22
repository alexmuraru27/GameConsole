#include "wifi_update.h"

#include <stdio.h>

#include "menu_common.h"
#include "buzzer.h"
#include "sysclock.h"
#include "logger.h"
#include "loader.h"
#include "esp_flasher.h"
#include "downloader.h"
#include "flash_ui.h"
#include "game_console.h"
#include "sd_layout.h"
#include "ff.h"

/* The firmware image is looked up under Firmware/ on the SD card (where Download
 * WiFi firmware / Poll Updates place it). */
#define ESP_IMAGE_PATH SD_DIR_FIRMWARE "/" ESP_FIRMWARE_FILENAME

#define UI_TITLE "WIFI UPDATE"

static const uint16_t s_done_notes[] = {NOTE_C5, 80U, NOTE_E5, 80U, NOTE_G5, 120U};
static const uint16_t s_fail_notes[] = {NOTE_A4, 120U, NOTE_E4, 200U};

/* esp_flasher progress callback: redraw the bar each block. */
static void onProgress(uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    flashUiScreen(UI_TITLE, "Flashing...", g_menu_pal_item_sel, true, done, total, "do not power off");
}

/* Verify the on-card image before touching the ESP. A flaky SD read can return
 * corrupt bytes, and the flasher's post-flash MD5 only proves the ESP stored
 * what we *read off the card* — not that the card gave us the real image. So a
 * corrupt read would silently flash a broken firmware (bricking the ESP) and
 * still report success. Recompute the file's CRC-32 and compare it to the CRC
 * recorded at download time; on a mismatch, warn and let the user choose.
 * Returns true if the flash should proceed. */
static bool preflashImageOk(void)
{
    uint32_t want_crc = 0U;
    if (!flashUiRecordedCrc(ESP_FIRMWARE_FILENAME, &want_crc))
    {
        /* No download record (e.g. the image was copied onto the card by hand):
         * no reference CRC to check against. Proceed — the flasher's MD5 still
         * verifies the transfer — but flag that the pre-check was skipped. */
        LOGGER_LOG_WARN(LOGGER_FLASHER, "no CRC record for '%s'; flashing without pre-verify", ESP_IMAGE_PATH);
        return true;
    }

    flashUiScreen(UI_TITLE, "Verifying image...", g_menu_pal_item_sel, false, 0U, 0U, NULL);
    bool exists = false;
    const uint32_t have_crc = downloaderLocalCrc(ESP_IMAGE_PATH, &exists);
    if (!exists || have_crc != want_crc)
    {
        LOGGER_LOG_WARN(LOGGER_FLASHER, "pre-flash CRC mismatch on '%s': have %08lX want %08lX (asking user)",
                        ESP_IMAGE_PATH, (unsigned long)have_crc, (unsigned long)want_crc);
        const bool proceed = flashUiConfirmMismatch(UI_TITLE, have_crc, want_crc);
        LOGGER_LOG_INFO(LOGGER_FLASHER, "user chose to %s", proceed ? "flash anyway" : "cancel");
        return proceed;
    }
    LOGGER_LOG_INFO(LOGGER_FLASHER, "pre-flash CRC ok (%08lX)", (unsigned long)want_crc);
    return true;
}

void wifiUpdateRun(void)
{
    menuResetSurface();
    LOGGER_LOG_INFO(LOGGER_FLASHER, "WiFi update requested");

    /* Need the card mounted and the image present before touching the ESP. */
    FILINFO info;
    if (!loaderMediaPresent() || f_stat(ESP_IMAGE_PATH, &info) != FR_OK)
    {
        LOGGER_LOG_WARN(LOGGER_FLASHER, "'%s' not found on SD", ESP_IMAGE_PATH);
        buzzerPlay(0U, false, s_fail_notes, 2U);
        flashUiWaitBack(UI_TITLE, "ESP01.bin not found on SD", g_menu_pal_alert);
        return;
    }

    /* Verify the image against its download CRC; on a mismatch preflashImageOk
     * warns and asks the user. A false return means they chose to cancel. */
    if (!preflashImageOk())
    {
        return;
    }

    /* Connect + erase can take a few seconds with no progress callback yet. */
    flashUiScreen(UI_TITLE, "Connecting to ESP...", g_menu_pal_item_sel, false, 0U, 0U, "do not power off");

    const EspFlashStatus status = espFlasherFlashFile(ESP_IMAGE_PATH, onProgress, NULL);

    if (status == ESP_FLASH_OK)
    {
        /* Keep Firmware/ESP01.bin on the card so it can be re-flashed without
         * re-downloading; it's in its own folder, so it never clutters the game
         * list (which only reads Games/). */
        LOGGER_LOG_INFO(LOGGER_FLASHER, "flash OK, keeping '%s' for re-flash", ESP_IMAGE_PATH);
        buzzerPlay(0U, false, s_done_notes, 3U);
        /* Reboot the whole console so the freshly-flashed ESP starts clean; the
         * reboot power-cycles the ESP as part of bringing the link back up. */
        flashUiScreen(UI_TITLE, "Update complete - rebooting", g_menu_pal_accent, false, 0U, 0U, NULL);
        delay(1500U);
        gameConsoleReboot(); /* does not return */
    }
    else
    {
        char line[40];
        snprintf(line, sizeof(line), "Failed: %s", espFlasherStatusString(status));
        buzzerPlay(0U, false, s_fail_notes, 2U);
        flashUiWaitBack(UI_TITLE, line, g_menu_pal_alert);
    }
}
