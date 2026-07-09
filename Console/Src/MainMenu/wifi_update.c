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

/* esp_flasher progress callback: redraw the bar each block. */
static void onProgress(uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    flashUiScreen(UI_TITLE, "Flashing...", g_menu_pal_item_sel, true, done, total, "do not power off");
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
        flashUiFail(UI_TITLE, "ESP01.bin not found on SD");
        return;
    }

    /* Verify the on-card image against its download CRC before touching the ESP:
     * the flasher's post-flash MD5 only proves the ESP stored what we *read off the
     * card*, so a flaky-SD read could otherwise brick the ESP and still report
     * success. On a mismatch flashUiPreflashConfirm warns and asks the user; a
     * false return means they cancelled. */
    flashUiScreen(UI_TITLE, "Verifying image...", g_menu_pal_item_sel, false, 0U, 0U, NULL);
    bool exists = false;
    const uint32_t have_crc = downloaderLocalCrc(ESP_IMAGE_PATH, &exists);
    if (!flashUiPreflashConfirm(UI_TITLE, ESP_FIRMWARE_FILENAME, have_crc))
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
        flashUiFail(UI_TITLE, line);
    }
}
