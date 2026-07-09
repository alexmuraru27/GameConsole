#include "Flasher/esp_flasher_port.h"
#include "Devices/esp01.h"

#include <stddef.h>

#include "Peripherals/usart.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"

/* Bootstrap timing — generous holds; flashing is a one-shot modal operation. */
#define ESP_RESET_HOLD_MS 100U
#define ESP_BOOT_HOLD_MS 50U

typedef struct
{
    esp_loader_port_t base; /* embedded handle; recovered via container_of */
    uint32_t time_end;      /* deadline for the one-shot timeout timer */
} EspFlasherPort;

static esp_loader_error_t portInit(esp_loader_port_t *port)
{
    (void)port;
    /* Pins are configured in gpioInit(); bring up / reset the UART itself. */
    usartInit();
    return ESP_LOADER_SUCCESS;
}

static void portEnterBootloader(esp_loader_port_t *port)
{
    (void)port;
    /* IO0 low while reset is released => ESP samples it and enters the ROM
     * download loader. IO2 stays high (set at init). */
    esp01SetEnable(true);
    esp01SetBootloader(true);
    esp01SetReset(true);
    delay(ESP_RESET_HOLD_MS);
    esp01SetReset(false);
    delay(ESP_BOOT_HOLD_MS);
    esp01SetBootloader(false);
}

static void portResetTarget(esp_loader_port_t *port)
{
    (void)port;
    esp01SetBootloader(false);
    esp01SetReset(true);
    delay(ESP_RESET_HOLD_MS);
    esp01SetReset(false);
}

static void portStartTimer(esp_loader_port_t *port, uint32_t ms)
{
    EspFlasherPort *p = container_of(port, EspFlasherPort, base);
    p->time_end = getSysTime() + ms;
}

static uint32_t portRemainingTime(esp_loader_port_t *port)
{
    EspFlasherPort *p = container_of(port, EspFlasherPort, base);
    const uint32_t now = getSysTime();
    return (now >= p->time_end) ? 0U : (p->time_end - now);
}

static void portDelay(esp_loader_port_t *port, uint32_t ms)
{
    (void)port;
    delay(ms);
}

static esp_loader_error_t portWrite(esp_loader_port_t *port, const uint8_t *data,
                                    uint16_t size, uint32_t timeout)
{
    (void)port;
    return usartWriteBytes(data, size, timeout) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}

static esp_loader_error_t portRead(esp_loader_port_t *port, uint8_t *data,
                                   uint16_t size, uint32_t timeout)
{
    (void)port;
    /* timeout is the budget for the whole read; charge each byte the remainder. */
    const uint32_t deadline = getSysTime() + timeout;

    for (uint16_t i = 0U; i < size; i++)
    {
        const uint32_t now = getSysTime();
        if (now >= deadline)
        {
            return ESP_LOADER_ERROR_TIMEOUT;
        }
        const int byte = usartReadByte(deadline - now);
        if (byte < 0)
        {
            return ESP_LOADER_ERROR_TIMEOUT;
        }
        data[i] = (uint8_t)byte;
    }

    return ESP_LOADER_SUCCESS;
}

static esp_loader_error_t portChangeRate(esp_loader_port_t *port, uint32_t rate)
{
    (void)port;
    usartSetBaud(rate);
    return ESP_LOADER_SUCCESS;
}

/* log / log_hex are NULL: the library stays silent and the high-level
 * esp_flasher.c narrates the operation on the LOGGER_FLASHER channel. */
static const esp_loader_port_ops_t s_ops = {
    .init = portInit,
    .deinit = NULL,
    .enter_bootloader = portEnterBootloader,
    .reset_target = portResetTarget,
    .start_timer = portStartTimer,
    .remaining_time = portRemainingTime,
    .delay_ms = portDelay,
    .log = NULL,
    .log_hex = NULL,
    .change_transmission_rate = portChangeRate,
    .write = portWrite,
    .read = portRead,
    .spi_set_cs = NULL,
    .sdio_write = NULL,
    .sdio_read = NULL,
    .sdio_card_init = NULL,
};

static EspFlasherPort s_port = {.base = {.ops = &s_ops}};

esp_loader_port_t *espFlasherPortGet(void)
{
    return &s_port.base;
}
