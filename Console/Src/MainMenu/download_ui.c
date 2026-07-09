#include "MainMenu/download_ui.h"

#include <stdio.h>

#include "MainMenu/menu_common.h"
#include "Renderer/renderer.h"
#include "Fonts/fonts.h"

#define DL_BAR_X 40
#define DL_BAR_Y 120
#define DL_BAR_W 240
#define DL_BAR_ROWS 3 /* stacked MENU_BAR_H rows for a thicker bar */

void downloadUiProgress(const char *title, const char *file, uint32_t done, uint32_t total, uint32_t bytes_per_sec)
{
    char line[40];
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, title);

    if (file != NULL)
    {
        menuDrawTextCentered(&font8x8, 92, g_menu_pal_item_sel, file);
    }

    n = menuDrawProgressBar(n, DL_BAR_X, DL_BAR_Y, DL_BAR_W, DL_BAR_ROWS, done, total);

    /* "done/total KB" and percentage. */
    const uint32_t pct = (total > 0U) ? (done * 100U / total) : 0U;
    snprintf(line, sizeof(line), "%lu / %lu KB   %lu%%",
             (unsigned long)(done / 1024U), (unsigned long)(total / 1024U), (unsigned long)pct);
    menuDrawTextCentered(&font8x8, (int16_t)(DL_BAR_Y + DL_BAR_ROWS * MENU_BAR_H + 8), g_menu_pal_item, line);

    /* Speed. */
    snprintf(line, sizeof(line), "%lu KB/s", (unsigned long)(bytes_per_sec / 1024U));
    menuDrawTextCentered(&font8x8, (int16_t)(DL_BAR_Y + DL_BAR_ROWS * MENU_BAR_H + 28), g_menu_pal_accent, line);

    menuDrawFooter("downloading...   do not power off");

    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

void downloadUiInfo(const char *title, const char *line, const uint16_t *palette)
{
    menuModalInfo(title, line, palette);
}

void downloadUiWait(const char *title, const char *line, const uint16_t *palette)
{
    menuModalWaitBack(title, line, palette);
}
