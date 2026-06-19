#include "wifi_update.h"

#include <stdio.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"
#include "loader.h"
#include "esp_flasher.h"
#include "ff.h"

/* The firmware image is looked up by this fixed name at the SD card root. */
#define ESP_IMAGE_PATH "ESP01.bin"

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
            n = menuDrawBar(n, BAR_X, y, BAR_W, g_menu_pal_footer);           /* track */
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

    /* Connect + erase can take a few seconds with no progress callback yet. */
    drawScreen("Connecting to ESP...", g_menu_pal_item_sel, false, 0U, 0U, "do not power off");

    const EspFlashStatus status = espFlasherFlashFile(ESP_IMAGE_PATH, onProgress, NULL);

    if (status == ESP_FLASH_OK)
    {
        buzzerPlay(0U, false, s_done_notes, 3U);
        waitForBack("Update complete!", g_menu_pal_accent);
    }
    else
    {
        char line[40];
        snprintf(line, sizeof(line), "Failed: %s", espFlasherStatusString(status));
        buzzerPlay(0U, false, s_fail_notes, 2U);
        waitForBack(line, g_menu_pal_alert);
    }
}
