#include "wifi_update.h"

#include <stdio.h>
#include <string.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"
#include "loader.h"
#include "esp_flasher.h"
#include "downloader.h"
#include "game_console.h"
#include "sd_layout.h"
#include "ff.h"

/* The firmware image is looked up under Firmware/ on the SD card (where Download
 * WiFi firmware / Poll Updates place it). */
#define ESP_IMAGE_PATH SD_DIR_FIRMWARE "/" ESP_FIRMWARE_FILENAME

/* Progress-bar geometry. */
#define BAR_X 40
#define BAR_Y 130
#define BAR_W 240
#define BAR_ROWS 3 /* stacked MENU_BAR_H rows => BAR_ROWS*MENU_BAR_H px tall */

static const uint16_t s_done_notes[] = {NOTE_C5, 80U, NOTE_E5, 80U, NOTE_G5, 120U};
static const uint16_t s_fail_notes[] = {NOTE_A4, 120U, NOTE_E4, 200U};

/* One screen: title, a status line, an optional progress bar + percentage. */
static void drawScreen(const char *line, const uint16_t *line_pal,
                       bool show_bar, uint32_t done, uint32_t total,
                       const char *footer)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "WIFI UPDATE");

    const int16_t lx = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, line)) / 2);
    n = menuDrawText(n, &font8x8, lx, 96, line_pal, line);

    if (show_bar)
    {
        const uint32_t fill = (total > 0U) ? (uint32_t)((uint64_t)BAR_W * done / total) : 0U;
        for (int i = 0; i < BAR_ROWS; i++)
        {
            const int16_t y = (int16_t)(BAR_Y + i * MENU_BAR_H);
            n = menuDrawBar(n, BAR_X, y, BAR_W, g_menu_pal_footer); /* track */
            if (fill > 0U)
            {
                n = menuDrawBar(n, BAR_X, y, (uint16_t)fill, g_menu_pal_accent); /* fill */
            }
        }

        char pct[8];
        const uint32_t percent = (total > 0U) ? (done * 100U / total) : 0U;
        snprintf(pct, sizeof(pct), "%lu%%", (unsigned long)percent);
        const int16_t px = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, pct)) / 2);
        n = menuDrawText(n, &font8x8, px, (int16_t)(BAR_Y + BAR_ROWS * MENU_BAR_H + 8), g_menu_pal_item_sel, pct);
    }

    if (footer != NULL)
    {
        n = menuDrawFooter(n, footer);
    }

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

/* esp_flasher progress callback: redraw the bar each block. */
static void onProgress(uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    drawScreen("Flashing...", g_menu_pal_item_sel, true, done, total, "do not power off");
}

/* CRC-32 recorded for the ESP firmware when it was last downloaded. The local
 * download manifest (downloaded.csv) keys on the *remote* path (e.g.
 * "wifi/ESP01.bin"), so match by basename against our SD filename. Returns true
 * and sets *crc if a record exists. */
static bool recordedFirmwareCrc(uint32_t *crc)
{
    static DownloadedEntry entries[24];
    const int count = downloaderLoadDownloaded(entries, (int)(sizeof(entries) / sizeof(entries[0])));
    for (int i = 0; i < count; i++)
    {
        const char *slash = strrchr(entries[i].path, '/');
        const char *base = (slash != NULL) ? slash + 1 : entries[i].path;
        if (strcasecmp(base, ESP_FIRMWARE_FILENAME) == 0)
        {
            *crc = entries[i].crc32;
            return true;
        }
    }
    return false;
}

/* Warn about a CRC mismatch and let the user decide. The image can legitimately
 * differ from the recorded CRC — it may have been copied onto the card by hand,
 * or the download record may be stale — but a flaky-SD corruption looks exactly
 * the same, so we surface both CRCs and leave the call to the user rather than
 * hard-blocking. Returns true to flash anyway (Special Button 1), false to
 * cancel (Special Button 2). */
static bool confirmFlashAnyway(uint32_t have, uint32_t want)
{
    char detail[40];
    snprintf(detail, sizeof(detail), "have %08lX  expect %08lX",
             (unsigned long)have, (unsigned long)want);
    buzzerPlay(0U, false, s_fail_notes, 2U);

    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    while (true)
    {
        const MenuNav nav = menuPollNav();
        if (nav.enter)
        {
            return true;
        }
        if (nav.back)
        {
            return false;
        }

        uint16_t n = 0U;
        rendererClear();
        n = menuDrawTitle(n, "WIFI UPDATE");
        const char *warn = "Image CRC mismatch";
        const int16_t wx = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, warn)) / 2);
        n = menuDrawText(n, &font8x8, wx, 84, g_menu_pal_alert, warn);
        const int16_t dx = (int16_t)((screen_w - (int16_t)menuTextWidth(font8x8.size, detail)) / 2);
        n = menuDrawText(n, &font8x8, dx, 108, g_menu_pal_footer, detail);
        n = menuDrawFooter(n, "A: flash anyway   B: cancel");
        rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
        rendererRender();
    }
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
    if (!recordedFirmwareCrc(&want_crc))
    {
        /* No download record (e.g. the image was copied onto the card by hand):
         * no reference CRC to check against. Proceed — the flasher's MD5 still
         * verifies the transfer — but flag that the pre-check was skipped. */
        LOGGER_LOG_WARN(LOGGER_FLASHER, "no CRC record for '%s'; flashing without pre-verify", ESP_IMAGE_PATH);
        return true;
    }

    drawScreen("Verifying image...", g_menu_pal_item_sel, false, 0U, 0U, NULL);
    bool exists = false;
    const uint32_t have_crc = downloaderLocalCrc(ESP_IMAGE_PATH, &exists);
    if (!exists || have_crc != want_crc)
    {
        LOGGER_LOG_WARN(LOGGER_FLASHER, "pre-flash CRC mismatch on '%s': have %08lX want %08lX (asking user)",
                        ESP_IMAGE_PATH, (unsigned long)have_crc, (unsigned long)want_crc);
        const bool proceed = confirmFlashAnyway(have_crc, want_crc);
        LOGGER_LOG_INFO(LOGGER_FLASHER, "user chose to %s", proceed ? "flash anyway" : "cancel");
        return proceed;
    }
    LOGGER_LOG_INFO(LOGGER_FLASHER, "pre-flash CRC ok (%08lX)", (unsigned long)want_crc);
    return true;
}

/* Hold a final screen until Special Button 2 is pressed. */
static void waitForBack(const char *line, const uint16_t *line_pal)
{
    while (true)
    {
        const MenuNav nav = menuPollNav();
        if (nav.back)
        {
            return;
        }
        drawScreen(line, line_pal, false, 0U, 0U, "Special Button 2: back");
    }
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
        waitForBack("ESP01.bin not found on SD", g_menu_pal_alert);
        return;
    }

    /* Verify the image against its download CRC; on a mismatch preflashImageOk
     * warns and asks the user. A false return means they chose to cancel. */
    if (!preflashImageOk())
    {
        return;
    }

    /* Connect + erase can take a few seconds with no progress callback yet. */
    drawScreen("Connecting to ESP...", g_menu_pal_item_sel, false, 0U, 0U, "do not power off");

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
        drawScreen("Update complete - rebooting", g_menu_pal_accent, false, 0U, 0U, NULL);
        delay(1500U);
        gameConsoleReboot(); /* does not return */
    }
    else
    {
        char line[40];
        snprintf(line, sizeof(line), "Failed: %s", espFlasherStatusString(status));
        buzzerPlay(0U, false, s_fail_notes, 2U);
        waitForBack(line, g_menu_pal_alert);
    }
}
