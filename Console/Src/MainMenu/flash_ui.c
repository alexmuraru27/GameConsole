#include "flash_ui.h"

#include <stdio.h>
#include <string.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "fonts.h"
#include "downloader.h"

/* Progress-bar geometry. */
#define BAR_X 40
#define BAR_Y 130
#define BAR_W 240
#define BAR_ROWS 3 /* stacked MENU_BAR_H rows => BAR_ROWS*MENU_BAR_H px tall */

static const uint16_t s_fail_notes[] = {NOTE_A4, 120U, NOTE_E4, 200U};

void flashUiScreen(const char *title, const char *line, const uint16_t *line_pal,
                   bool show_bar, uint32_t done, uint32_t total, const char *footer)
{
    const int16_t screen_w = (int16_t)rendererGetWidthPixels();
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, title);

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

bool flashUiConfirmMismatch(const char *title, uint32_t have, uint32_t want)
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
        n = menuDrawTitle(n, title);
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

void flashUiWaitBack(const char *title, const char *line, const uint16_t *line_pal)
{
    while (true)
    {
        const MenuNav nav = menuPollNav();
        if (nav.back)
        {
            return;
        }
        flashUiScreen(title, line, line_pal, false, 0U, 0U, "Special Button 2: back");
    }
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
