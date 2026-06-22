#ifndef __OS_FLASHER_H
#define __OS_FLASHER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Console OS self-flashing (application side). A new firmware image is streamed
 * off the SD card into the staging region of internal flash and verified there by
 * readback; committing it writes the staging header and reboots into the
 * bootloader, which performs the actual, power-fail-safe apply (see flash_map.h
 * and Bootloader/). Blocking and modal — driven from MainMenu/os_update.c.
 *
 * The running OS is never overwritten by this code: it only writes the staging
 * region (sectors it is not executing from). The irreversible swap happens in the
 * bootloader after a reboot, reading from the already-verified staging copy.
 */

/* The OS image the console looks for under Firmware/ on the SD card. Shares the
 * .bin extension with games but lives in its own folder, so it never appears in
 * the game list. */
#define CONSOLE_FIRMWARE_FILENAME "Console.bin"

typedef enum
{
    OS_FLASH_OK = 0,      /* staged and verified by readback */
    OS_FLASH_NO_FILE,     /* image missing / empty / unreadable on the card */
    OS_FLASH_TOO_BIG,     /* image does not fit the app region */
    OS_FLASH_READ_FAIL,   /* SD read error mid-stream */
    OS_FLASH_WRITE_FAIL,  /* flash erase/program error */
    OS_FLASH_VERIFY_FAIL, /* staging readback CRC did not match what was written */
} OsFlashStatus;

/* Progress callback: invoked during streaming with bytes done / total. */
typedef void (*OsFlashProgressCb)(uint32_t done, uint32_t total, void *ctx);

/*
 * Stream the image at `path` (FatFs path) into the staging region and verify it by
 * readback. On OS_FLASH_OK, *out_crc holds the CRC-32 of the bytes read and
 * *out_size their count. Does NOT commit or reboot — the caller decides whether
 * the staged image is the intended one (e.g. by comparing *out_crc to a recorded
 * value) before committing. `cb` may be NULL.
 */
OsFlashStatus osFlasherStage(const char *path, uint32_t *out_crc, uint32_t *out_size,
                             OsFlashProgressCb cb, void *ctx);

/*
 * Commit the staged image (size + CRC from osFlasherStage) by writing the staging
 * header, then reboot into the bootloader to apply it. Returns only on failure to
 * write/verify the header (OS_FLASH_WRITE_FAIL); on success it does not return.
 */
OsFlashStatus osFlasherCommitAndReboot(uint32_t image_size, uint32_t image_crc32);

/* Human-readable one-liner for a status code (for UI / logs). */
const char *osFlasherStatusString(OsFlashStatus status);

#endif /* __OS_FLASHER_H */
