/*
 * ESP-01S firmware — GameConsole WiFi module.
 *
 * Milestone: a blinky + UART heartbeat. The console flashes this image onto the
 * ESP via Settings -> "Upgrade WiFi module" (it reads ESP01.bin off the SD card;
 * see ../docu/flasher.md), and "Test WiFi module" listens to the heartbeat below
 * to confirm the ESP is alive. The runtime console<->ESP protocol is future work
 * — this file already pulls in the shared contract so the wiring is in place.
 */
#include <Arduino.h>
#include "network_protocol.h"

/*
 * The ESP-01S on-board LED is on GPIO2 and is active-low (drive LOW = on).
 * NOTE: do NOT use LED_BUILTIN here — for this board's 'generic' variant it maps
 * to GPIO1, which is the UART0 TX pin (owned by Serial), so it neither matches
 * the physical LED nor is free to toggle.
 */
static const uint8_t LED_PIN = 2;

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); /* LED off */

    /* UART0 (GPIO1 TX / GPIO3 RX) -> wired to the console's USART1. */
    Serial.begin(NETWORK_UART_BAUD);
    Serial.printf("\nESP-01S firmware up (protocol v%u)\n", NETWORK_PROTOCOL_VERSION);
}

void loop()
{
    static uint32_t beat = 0;

    digitalWrite(LED_PIN, LOW); /* LED on  */
    Serial.printf("blink %lu\n", (unsigned long)beat++);
    delay(500);

    digitalWrite(LED_PIN, HIGH); /* LED off */
    delay(500);
}
