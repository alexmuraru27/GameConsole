#ifndef __ESP_FLASHER_H
#define __ESP_FLASHER_H

#include <stdint.h>

/*
 * High-level ESP-01 firmware flashing. Streams a firmware image off the SD
 * card and programs it into the ESP-01 over USART1 via the esp-serial-flasher
 * library. Blocking and modal — intended to be driven from a dedicated UI flow
 * (see MainMenu/wifi_update.c). The SD card must already be mounted.
 */

/* The ESP firmware image the console looks for at the SD-card root. It shares
 * the .bin extension with games but is rejected at launch by the game loader's
 * magic/ABI check, so it is harmless if it appears in the list; the upgrade flow
 * removes it on success. */
#define ESP_FIRMWARE_FILENAME "ESP01.bin"

typedef enum
{
    ESP_FLASH_OK = 0,       /* programmed and verified */
    ESP_FLASH_NO_FILE,      /* image not found / unreadable on the card */
    ESP_FLASH_CONNECT_FAIL, /* could not sync with the ESP ROM bootloader */
    ESP_FLASH_WRITE_FAIL,   /* erase/write/finish failed */
    ESP_FLASH_VERIFY_FAIL,  /* MD5 mismatch after programming */
} EspFlashStatus;

/* Progress callback: invoked after each block with bytes done / total. */
typedef void (*EspFlashProgressCb)(uint32_t done, uint32_t total, void *ctx);

/* Flash the image at `path` (FatFs path, e.g. "ESP01.bin") at flash offset 0.
 * `cb` may be NULL. Returns the outcome. */
EspFlashStatus espFlasherFlashFile(const char *path, EspFlashProgressCb cb, void *ctx);

/* Human-readable one-liner for a status code (for UI / logs). */
const char *espFlasherStatusString(EspFlashStatus status);

#endif /* __ESP_FLASHER_H */
