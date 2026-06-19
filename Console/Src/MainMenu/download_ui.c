#include "download_ui.h"

#include <stdio.h>

#include "menu_common.h"
#include "renderer.h"
#include "fonts.h"

#define DL_BAR_X 40
#define DL_BAR_Y 120
#define DL_BAR_W 240
#define DL_BAR_ROWS 3 /* stacked MENU_BAR_H rows for a thicker bar */

static int16_t centerX(const char *text)
{
    const int16_t w = (int16_t)rendererGetWidthPixels();
    return (int16_t)((w - (int16_t)menuTextWidth(font8x8.size, text)) / 2);
}

void downloadUiProgress(const char *title, const char *file, uint32_t done, uint32_t total, uint32_t bytes_per_sec)
{
    char line[40];
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, title);

    if (file != NULL)
    {
        n = menuDrawText(n, &font8x8, centerX(file), 92, g_menu_pal_item_sel, file);
    }

    /* Bar: dim track + accent fill proportional to done/total. */
    const uint32_t fill = (total > 0U) ? (uint32_t)(((uint64_t)DL_BAR_W * done) / total) : 0U;
    for (int i = 0; i < DL_BAR_ROWS; i++)
    {
        const int16_t y = (int16_t)(DL_BAR_Y + i * MENU_BAR_H);
        n = menuDrawBar(n, DL_BAR_X, y, DL_BAR_W, g_menu_pal_footer);
        if (fill > 0U)
        {
            n = menuDrawBar(n, DL_BAR_X, y, (uint16_t)fill, g_menu_pal_accent);
        }
    }

    /* "done/total KB" and percentage. */
    const uint32_t pct = (total > 0U) ? (done * 100U / total) : 0U;
    snprintf(line, sizeof(line), "%lu / %lu KB   %lu%%",
             (unsigned long)(done / 1024U), (unsigned long)(total / 1024U), (unsigned long)pct);
    n = menuDrawText(n, &font8x8, centerX(line), (int16_t)(DL_BAR_Y + DL_BAR_ROWS * MENU_BAR_H + 8), g_menu_pal_item, line);

    /* Speed. */
    snprintf(line, sizeof(line), "%lu KB/s", (unsigned long)(bytes_per_sec / 1024U));
    n = menuDrawText(n, &font8x8, centerX(line), (int16_t)(DL_BAR_Y + DL_BAR_ROWS * MENU_BAR_H + 28), g_menu_pal_accent, line);

    n = menuDrawFooter(n, "downloading...   do not power off");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

void downloadUiInfo(const char *title, const char *line, const uint16_t *palette)
{
    uint16_t n = 0U;
    rendererClear();
    n = menuDrawTitle(n, title);
    n = menuDrawText(n, &font8x8, centerX(line), 110, palette, line);
    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

void downloadUiWait(const char *title, const char *line, const uint16_t *palette)
{
    while (true)
    {
        const MenuNav nav = menuPollNav();
        if (nav.back)
        {
            return;
        }
        uint16_t n = 0U;
        rendererClear();
        n = menuDrawTitle(n, title);
        n = menuDrawText(n, &font8x8, centerX(line), 110, palette, line);
        n = menuDrawFooter(n, "Special Button 2: back");
        rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
        rendererRender();
    }
}
