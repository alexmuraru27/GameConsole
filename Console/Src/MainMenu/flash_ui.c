#include "MainMenu/flash_ui.h"

#include <stdio.h>
#include <string.h>

#include "MainMenu/menu_common.h"
#include "Renderer/renderer.h"
#include "Devices/buzzer.h"
#include "Fonts/fonts.h"
#include "Network/downloader.h"
#include "Logger/logger.h"

/* Progress-bar geometry. */
#define BAR_X 40
#define BAR_Y 130
#define BAR_W 240
#define BAR_ROWS 3 /* stacked MENU_BAR_H rows => BAR_ROWS*MENU_BAR_H px tall */

static const uint16_t s_fail_notes[] = {NOTE_A4, 120U, NOTE_E4, 200U};

void flashUiScreen(const char *title, const char *line, const uint16_t *line_pal,
                   bool show_bar, uint32_t done, uint32_t total, const char *footer)
{
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, title);
    menuDrawTextCentered(&font8x8, 96, line_pal, line);

    if (show_bar)
    {
        n = menuDrawProgressBar(n, BAR_X, BAR_Y, BAR_W, BAR_ROWS, done, total);

        char pct[8];
        const uint32_t percent = (total > 0U) ? (done * 100U / total) : 0U;
        snprintf(pct, sizeof(pct), "%lu%%", (unsigned long)percent);
        menuDrawTextCentered(&font8x8, (int16_t)(BAR_Y + BAR_ROWS * MENU_BAR_H + 8), g_menu_pal_item_sel, pct);
    }

    if (footer != NULL)
    {
        menuDrawFooter(footer);
    }

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

bool flashUiConfirmMismatch(const char *title, uint32_t have, uint32_t want)
{
    char detail[40];
    snprintf(detail, sizeof(detail), "have %08lX  expect %08lX",
             (unsigned long)have, (unsigned long)want);
    buzzerPlay(0U, false, s_fail_notes, 2U);

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
        n = menuDrawTitle(n, title);
        menuDrawTextCentered(&font8x8, 84, g_menu_pal_alert, "Image CRC mismatch");
        menuDrawTextCentered(&font8x8, 108, g_menu_pal_footer, detail);
        menuDrawFooter("A: flash anyway   B: cancel");
        rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
        rendererRender();
    }
}

void flashUiWaitBack(const char *title, const char *line, const uint16_t *line_pal)
{
    menuModalWaitBack(title, line, line_pal);
}

void flashUiFail(const char *title, const char *msg)
{
    buzzerPlay(0U, false, s_fail_notes, 2U);
    flashUiWaitBack(title, msg, g_menu_pal_alert);
}

bool flashUiPreflashConfirm(const char *title, const char *basename, uint32_t have_crc)
{
    uint32_t want_crc = 0U;
    if (!flashUiRecordedCrc(basename, &want_crc))
    {
        /* No download record (e.g. hand-copied onto the card): no reference CRC.
         * Proceed — the downstream flow still verifies the transfer — but flag it. */
        LOGGER_LOG_WARN(LOGGER_FLASHER, "no CRC record for '%s'; proceeding without pre-verify", basename);
        return true;
    }
    if (have_crc != want_crc)
    {
        LOGGER_LOG_WARN(LOGGER_FLASHER, "pre-flash CRC mismatch on '%s': have %08lX want %08lX (asking user)",
                        basename, (unsigned long)have_crc, (unsigned long)want_crc);
        const bool proceed = flashUiConfirmMismatch(title, have_crc, want_crc);
        LOGGER_LOG_INFO(LOGGER_FLASHER, "user chose to %s", proceed ? "flash anyway" : "cancel");
        return proceed;
    }
    LOGGER_LOG_INFO(LOGGER_FLASHER, "pre-flash CRC ok (%08lX)", (unsigned long)want_crc);
    return true;
}

bool flashUiRecordedCrc(const char *basename, uint32_t *crc)
{
    static DownloadedEntry entries[24];
    const int count = downloaderLoadDownloaded(entries, (int)(sizeof(entries) / sizeof(entries[0])));
    for (int i = 0; i < count; i++)
    {
        const char *slash = strrchr(entries[i].path, '/');
        const char *base = (slash != NULL) ? slash + 1 : entries[i].path;
        if (strcasecmp(base, basename) == 0)
        {
            *crc = entries[i].crc32;
            return true;
        }
    }
    return false;
}
