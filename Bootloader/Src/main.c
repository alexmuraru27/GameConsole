#include <stm32f407xx.h>
#include <stdbool.h>

#include "flash_map.h"
#include "flash_ll.h"
#include "crc.h"
#include "swo.h"
#include "logger.h"

/*
 * Self-flash bootloader. It runs first on every reset from sector 0 — which the
 * self-flasher never erases — and decides what to boot:
 *
 *   1. If staging holds a committed, ready update (valid OsStagingHeader), apply it:
 *      erase the app region, copy the staged image in, and CRC-verify the app by
 *      readback. Clear the pending flag ONLY on a verified match, then reset.
 *   2. Otherwise, if a valid app is present, hand control to it.
 *
 * Power-fail safety falls out of step 1 being idempotent: the staging copy is
 * intact internal flash, so an interrupted apply simply re-runs on the next boot
 * (pending still set, app CRC still wrong) until the readback verifies — at which
 * point the flag is cleared and the new app boots. The bootloader itself is never
 * rewritten by an update, so it cannot be corrupted by one.
 *
 * It runs at the reset-default clock (HSI 16 MHz, flash 0 wait states) with
 * interrupts left masked at their reset state, so there is nothing to configure.
 * Every decision and apply step is traced over SWO on the LOGGER_BOOT channel.
 */

/* startup.s calls SystemInit before .data init; nothing to set up here. */
void SystemInit(void)
{
}

/* A committed, self-consistent, in-range pending update sits in staging. */
static bool stagingPending(const OsStagingHeader *hdr)
{
    if (hdr->magic != OS_STAGING_MAGIC)
    {
        LOGGER_LOG_INFO(LOGGER_BOOT, "no pending update (staging magic=0x%08lX)", (unsigned long)hdr->magic);
        return false;
    }
    const uint32_t want = crc32_calculate((const uint8_t *)hdr, OS_STAGING_HEADER_CRC_LEN);
    if (want != hdr->header_crc32)
    {
        LOGGER_LOG_WARN(LOGGER_BOOT, "staging header CRC bad (have %08lX, calc %08lX) - ignoring",
                        (unsigned long)hdr->header_crc32, (unsigned long)want);
        return false;
    }
    if (hdr->image_size == 0U || hdr->image_size > APP_FLASH_SIZE)
    {
        LOGGER_LOG_WARN(LOGGER_BOOT, "staging size %lu out of range (max %lu) - ignoring",
                        (unsigned long)hdr->image_size, (unsigned long)APP_FLASH_SIZE);
        return false;
    }
    LOGGER_LOG_INFO(LOGGER_BOOT, "pending update: size=%lu B, crc=%08lX",
                    (unsigned long)hdr->image_size, (unsigned long)hdr->image_crc32);
    return true;
}

/* A plausible application is programmed: an initial SP in SRAM and a Thumb reset
 * vector inside the app region. */
static bool appPresent(void)
{
    const uint32_t sp = *(const volatile uint32_t *)APP_FLASH_ADDR;
    const uint32_t reset = *(const volatile uint32_t *)(APP_FLASH_ADDR + 4U);
    const bool sp_ok = (sp >= 0x20000000U) && (sp <= 0x20020000U);
    const bool reset_ok = (reset >= APP_FLASH_ADDR) &&
                          (reset < APP_FLASH_ADDR + APP_FLASH_SIZE) &&
                          ((reset & 1U) != 0U);
    LOGGER_LOG_DEBUG(LOGGER_BOOT, "app vector: SP=0x%08lX reset=0x%08lX valid=%d",
                     (unsigned long)sp, (unsigned long)reset, (int)(sp_ok && reset_ok));
    return sp_ok && reset_ok;
}

/* Apply the staged image into the app region, verify by readback, and reset. The
 * orchestration runs from sector 0 (never erased here); only the flash_ll erase/
 * program primitives run from RAM, so each operation completes while the flash
 * array is stalled. */
static void applyStaging(const OsStagingHeader *hdr)
{
    LOGGER_LOG_INFO(LOGGER_BOOT, "applying update -> app @ 0x%08lX (%lu B)",
                    (unsigned long)APP_FLASH_ADDR, (unsigned long)hdr->image_size);

    flashLlUnlock();

    LOGGER_LOG_INFO(LOGGER_BOOT, "erasing app sectors %lu..%lu",
                    (unsigned long)APP_SECTOR_FIRST, (unsigned long)APP_SECTOR_LAST);
    for (uint32_t s = APP_SECTOR_FIRST; s <= APP_SECTOR_LAST; s++)
    {
        const bool ok = flashLlEraseSector(s);
        LOGGER_LOG_DEBUG(LOGGER_BOOT, "  erase sector %lu %s", (unsigned long)s, ok ? "ok" : "FAIL");
    }

    /* Copy staging -> app, word by word, tracing every 32 KB so the progress is
     * visible without logging the per-word hot loop. */
    const uint32_t *src = (const uint32_t *)STAGING_IMAGE_ADDR;
    const uint32_t words = (hdr->image_size + 3U) / 4U;
    LOGGER_LOG_INFO(LOGGER_BOOT, "copying %lu B (%lu words) staging 0x%08lX -> app 0x%08lX",
                    (unsigned long)hdr->image_size, (unsigned long)words,
                    (unsigned long)STAGING_IMAGE_ADDR, (unsigned long)APP_FLASH_ADDR);
    uint32_t next_mark = 32U * 1024U;
    for (uint32_t i = 0U; i < words; i++)
    {
        flashLlProgramWord(APP_FLASH_ADDR + i * 4U, src[i]);
        const uint32_t done = (i + 1U) * 4U;
        if (done >= next_mark)
        {
            LOGGER_LOG_DEBUG(LOGGER_BOOT, "  copied %lu/%lu B", (unsigned long)done, (unsigned long)hdr->image_size);
            next_mark += 32U * 1024U;
        }
    }
    flashLlLock();
    LOGGER_LOG_INFO(LOGGER_BOOT, "copy complete; verifying app by readback");

    /* Verify the freshly programmed app by readback before enabling it. */
    const uint32_t app_crc = crc32_calculate((const uint8_t *)APP_FLASH_ADDR, hdr->image_size);
    if (app_crc == hdr->image_crc32)
    {
        LOGGER_LOG_INFO(LOGGER_BOOT, "verify OK (crc=%08lX); clearing pending, rebooting into new OS",
                        (unsigned long)app_crc);
        /* Mark the update consumed by clearing the staging magic (a single word
         * program, 1->0 only). A torn write here just leaves the header invalid, so
         * the next boot treats it as not-pending and runs the verified app. */
        flashLlUnlock();
        flashLlProgramWord(STAGING_FLASH_ADDR, 0x00000000U);
        flashLlLock();
    }
    else
    {
        LOGGER_LOG_ERROR(LOGGER_BOOT, "verify FAILED (app crc=%08lX, expected=%08lX); retrying after reset",
                         (unsigned long)app_crc, (unsigned long)hdr->image_crc32);
    }

    /* On success the pending flag is now clear, so the reset boots the new app. On
     * failure it is still set, so the reset re-applies from the intact staging.
     *
     * KNOWN LIMIT (docu/bootloader.md §9): no retry cap. Once the app region is
     * erased there is no old app to fall back to, so a genuinely failing flash chip
     * reset-loops, re-applying from the (good) staging copy. Accepted — a stable
     * supply completes the apply; a dead flash needs servicing regardless.
     *
     * NOTE (pending, cosmetic): the reset can cut off the last SWO log line
     * mid-byte — the serializer may still be draining and there is no SysTick delay
     * here to cover it. A short drain before the reset would fix it. */
    NVIC_SystemReset();
    for (;;)
    {
    }
}

/* Hand control to the application: relocate the vector table, load its stack
 * pointer and reset vector, and branch. PRIMASK is left at its reset default
 * (interrupts enabled) — the bootloader enables no interrupts of its own, and the
 * app's Reset_Handler expects the default-enabled state (it never re-enables). */
static void jumpToApp(void)
{
    const uint32_t sp = *(const volatile uint32_t *)APP_FLASH_ADDR;
    const uint32_t reset = *(const volatile uint32_t *)(APP_FLASH_ADDR + 4U);

    LOGGER_LOG_INFO(LOGGER_BOOT, "jumping to app @ 0x%08lX (reset=0x%08lX)",
                    (unsigned long)APP_FLASH_ADDR, (unsigned long)reset);

    SCB->VTOR = APP_FLASH_ADDR;
    __set_MSP(sp);
    __DSB();
    __ISB();
    ((void (*)(void))reset)();
}

int main(void)
{
    swoInit(2000000);
    LOGGER_LOG_INFO(LOGGER_BOOT, "=== bootloader start (boot=0x%08lX app=0x%08lX staging=0x%08lX) ===",
                    (unsigned long)BOOT_FLASH_ADDR, (unsigned long)APP_FLASH_ADDR, (unsigned long)STAGING_FLASH_ADDR);

    const OsStagingHeader *hdr = (const OsStagingHeader *)STAGING_FLASH_ADDR;

    if (stagingPending(hdr))
    {
        applyStaging(hdr); /* applies, verifies, resets; does not return */
    }

    if (appPresent())
    {
        jumpToApp(); /* does not return */
    }

    /* No valid app and no pending update — nothing to run (e.g. a fresh board with
     * only the bootloader flashed). Wait for an SWD attach. */
    LOGGER_LOG_ERROR(LOGGER_BOOT, "no valid app and no pending update; halting (flash an app over SWD)");
    for (;;)
    {
    }
}
