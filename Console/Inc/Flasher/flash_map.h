#ifndef __FLASH_MAP_H
#define __FLASH_MAP_H

#include <stdint.h>

/*
 * Internal-flash partition map — the single source of truth shared by the
 * bootloader (Bootloader/) and the application's OS self-flasher
 * (Console/Src/Flasher/os_flasher.c). Both must agree byte-for-byte, so the
 * addresses, sector numbers, and the staging header below live here only.
 *
 * STM32F407VET6: 512 KB flash, sectors {0-3:16K, 4:64K, 5-7:128K}.
 *
 *   sector 0     0x08000000  16K   bootloader   (SWD-flashed once; self-flash never touches it)
 *   sectors 1-5  0x08004000 240K   application  (the console OS; runs with VTOR here)
 *   sectors 6-7  0x08040000 256K   staging      (a committed update: header + new image)
 *
 * A self-update streams the new OS image into staging, verifies it by readback,
 * then commits a header and reboots. The bootloader applies a committed staging
 * image into the app region, re-verifies it by readback CRC, and only then runs
 * it — re-applying from the intact staging if power is lost mid-apply. So the
 * irreversible step reads from reliable internal flash, never the SD card, and is
 * idempotent against interruption.
 */

#define FLASH_BANK_BASE 0x08000000U

/* Bootloader: owns the reset vector. Off-limits to the self-flasher. */
#define BOOT_FLASH_ADDR 0x08000000U
#define BOOT_FLASH_SIZE (16U * 1024U) /* sector 0 */

/* Application: the console OS. The app's startup sets SCB->VTOR to APP_FLASH_ADDR. */
#define APP_FLASH_ADDR 0x08004000U
#define APP_FLASH_SIZE (240U * 1024U) /* sectors 1-5 */
#define APP_SECTOR_FIRST 1U
#define APP_SECTOR_LAST 5U

/* Staging: scratch region a self-update writes before committing. */
#define STAGING_FLASH_ADDR 0x08040000U
#define STAGING_FLASH_SIZE (256U * 1024U) /* sectors 6-7 */
#define STAGING_SECTOR_FIRST 6U
#define STAGING_SECTOR_LAST 7U

/* The OsStagingHeader sits at the start of staging; the image follows it. The
 * offset is generous and flash-word aligned, leaving the header room to grow. */
#define STAGING_HEADER_OFFSET 0U
#define STAGING_IMAGE_OFFSET 0x200U
#define STAGING_IMAGE_ADDR (STAGING_FLASH_ADDR + STAGING_IMAGE_OFFSET)

/* An OS image must fit the app region (the bootloader copies it there) and the
 * staging image area. The app region is the binding limit. */
#define OS_IMAGE_MAX_SIZE APP_FLASH_SIZE

/* Marks a committed, ready-to-apply staging image ("OSUP"). The bootloader clears
 * it (programs it to 0) once the update has been applied and readback-verified. */
#define OS_STAGING_MAGIC 0x4F535550U

/*
 * Staging header at STAGING_FLASH_ADDR. Written last when committing an update, so
 * a partially-written or absent header reads as "no pending update" and the old OS
 * boots untouched. `header_crc32` covers the three words above it, so a torn write
 * of the header itself is also rejected.
 */
typedef struct
{
    uint32_t magic;        /* OS_STAGING_MAGIC when a pending update is committed */
    uint32_t image_size;   /* bytes of the image at STAGING_IMAGE_ADDR */
    uint32_t image_crc32;  /* CRC-32 (zlib/IEEE) the applied app must match */
    uint32_t header_crc32; /* CRC-32 of the 12 bytes above */
} OsStagingHeader;

#define OS_STAGING_HEADER_CRC_LEN 12U /* magic + image_size + image_crc32 */

#endif /* __FLASH_MAP_H */
