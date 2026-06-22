#ifndef __FLASH_UI_H
#define __FLASH_UI_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Shared modal UI for the firmware self-flash flows — "Upgrade WiFi module"
 * (wifi_update.c) and "Upgrade OS" (os_update.c). Both present the same thing: a
 * status line under a title, an optional progress bar with percentage, a
 * CRC-mismatch confirm prompt, and a wait-for-back screen; both look up the CRC
 * recorded at download time to pre-verify the on-card image. Each flow passes its
 * own title and image basename.
 */

/* Status line under `title`, with an optional progress bar + percentage and an
 * optional footer hint. */
void flashUiScreen(const char *title, const char *line, const uint16_t *line_pal,
                   bool show_bar, uint32_t done, uint32_t total, const char *footer);

/* Warn that the on-card image's CRC differs from the recorded one and let the
 * user decide. Returns true to flash anyway (Special Button 1), false to cancel
 * (Special Button 2). */
bool flashUiConfirmMismatch(const char *title, uint32_t have, uint32_t want);

/* Render `line` under `title` and block until Special Button 2 is pressed. */
void flashUiWaitBack(const char *title, const char *line, const uint16_t *line_pal);

/* CRC-32 recorded for a firmware file when it was last downloaded. The local
 * download manifest (downloaded.csv) keys on the remote path (e.g. "os/Console.bin"),
 * so match by `basename` against our SD filename. Returns true and sets *crc if a
 * record exists. */
bool flashUiRecordedCrc(const char *basename, uint32_t *crc);

#endif /* __FLASH_UI_H */
