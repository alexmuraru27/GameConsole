#ifndef __DOWNLOAD_UI_H
#define __DOWNLOAD_UI_H

#include <stdint.h>

/*
 * Shared full-screen UI for the WiFi download flows (Poll Remote Games and
 * Download WiFi firmware): a progress bar with bytes done/total and speed, plus
 * simple info / wait-for-button screens. Used as a DownloadProgressCb target.
 */

/* One progress frame: title, current file, a bar, "done/total KB" and "KB/s". */
void downloadUiProgress(const char *title, const char *file, uint32_t done, uint32_t total, uint32_t bytes_per_sec);

/* Render a single centered message (no input). */
void downloadUiInfo(const char *title, const char *line, const uint16_t *palette);

/* Render a centered message and block until Special Button 2. */
void downloadUiWait(const char *title, const char *line, const uint16_t *palette);

#endif /* __DOWNLOAD_UI_H */
