#include "Flasher/esp_flasher.h"

#include <string.h>

#include "Flasher/esp_flasher_port.h"
#include "esp_loader.h"
#include "ff.h"
#include "Peripherals/watchdog.h"
#include "Logger/logger.h"

/* Block size handed to the bootloader per write. 1 KB is the upstream default
 * and a comfortable fit for the ESP8266 ROM/stub receive buffer. */
#define FLASH_BLOCK_SIZE 1024U

/* ESP8266 Arduino/PlatformIO images are a single blob flashed from address 0. */
#define FLASH_OFFSET 0U

/* One reusable transfer buffer; flashing is strictly sequential. */
static uint8_t s_payload[FLASH_BLOCK_SIZE];

const char *espFlasherStatusString(EspFlashStatus status)
{
    switch (status)
    {
    case ESP_FLASH_OK:
        return "OK";
    case ESP_FLASH_NO_FILE:
        return "image not found";
    case ESP_FLASH_CONNECT_FAIL:
        return "ESP not responding";
    case ESP_FLASH_WRITE_FAIL:
        return "write failed";
    case ESP_FLASH_VERIFY_FAIL:
        return "verify failed";
    default:
        return "unknown";
    }
}

EspFlashStatus espFlasherFlashFile(const char *path, EspFlashProgressCb cb, void *ctx)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "cannot open '%s'", path);
        return ESP_FLASH_NO_FILE;
    }

    const uint32_t file_size = (uint32_t)f_size(&file);
    /* The bootloader needs a 4-byte-aligned image; pad the tail with 0xFF. */
    const uint32_t image_size = (file_size + 3U) & ~3U;
    LOGGER_LOG_INFO(LOGGER_FLASHER, "flashing '%s' (%u bytes)", path, (unsigned)file_size);

    esp_loader_t loader;
    if (esp_loader_init_serial(&loader, espFlasherPortGet()) != ESP_LOADER_SUCCESS)
    {
        f_close(&file);
        return ESP_FLASH_CONNECT_FAIL;
    }

    /* The sync + RAM-stub upload handshake has no progress callback and can take
     * a second or more of retries; kick before entering it. */
    watchdogKick();
    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    esp_loader_error_t err = esp_loader_connect_with_stub(&loader, &connect_args);
    if (err != ESP_LOADER_SUCCESS)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "connect failed (%d)", (int)err);
        f_close(&file);
        return ESP_FLASH_CONNECT_FAIL;
    }
    LOGGER_LOG_INFO(LOGGER_FLASHER, "connected, target=%d", (int)esp_loader_get_target(&loader));

    esp_loader_flash_cfg_t cfg = {
        .offset = FLASH_OFFSET,
        .image_size = image_size,
        .block_size = FLASH_BLOCK_SIZE,
        .skip_verify = false,
    };
    err = esp_loader_flash_start(&loader, &cfg);
    if (err != ESP_LOADER_SUCCESS)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "flash_start failed (%d)", (int)err);
        f_close(&file);
        return ESP_FLASH_WRITE_FAIL;
    }

    uint32_t written = 0U;
    while (written < image_size)
    {
        watchdogKick(); /* per-block SD read + UART program: feed the watchdog */
        const uint32_t remaining = image_size - written;
        const uint32_t chunk = (remaining < FLASH_BLOCK_SIZE) ? remaining : FLASH_BLOCK_SIZE;

        UINT read_bytes = 0U;
        if (f_read(&file, s_payload, chunk, &read_bytes) != FR_OK)
        {
            LOGGER_LOG_ERROR(LOGGER_FLASHER, "SD read failed at %u", (unsigned)written);
            f_close(&file);
            return ESP_FLASH_WRITE_FAIL;
        }
        /* Pad the final short block out to the aligned size. */
        if (read_bytes < chunk)
        {
            memset(s_payload + read_bytes, 0xFF, chunk - read_bytes);
        }

        err = esp_loader_flash_write(&loader, &cfg, s_payload, chunk);
        if (err != ESP_LOADER_SUCCESS)
        {
            LOGGER_LOG_ERROR(LOGGER_FLASHER, "flash_write failed at %u (%d)", (unsigned)written, (int)err);
            f_close(&file);
            return ESP_FLASH_WRITE_FAIL;
        }

        written += chunk;
        if (cb != NULL)
        {
            cb(written, image_size, ctx);
        }
    }

    err = esp_loader_flash_finish(&loader, &cfg);
    f_close(&file);
    if (err == ESP_LOADER_ERROR_INVALID_MD5)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "MD5 verify failed");
        return ESP_FLASH_VERIFY_FAIL;
    }
    if (err != ESP_LOADER_SUCCESS)
    {
        LOGGER_LOG_ERROR(LOGGER_FLASHER, "flash_finish failed (%d)", (int)err);
        return ESP_FLASH_WRITE_FAIL;
    }

    /* Boot the freshly-flashed firmware (IO0 high, pulse reset). */
    esp_loader_reset_target(&loader);
    LOGGER_LOG_INFO(LOGGER_FLASHER, "flash complete, %u bytes verified", (unsigned)image_size);
    return ESP_FLASH_OK;
}
