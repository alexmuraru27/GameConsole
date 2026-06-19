#ifndef __WIFI_UPDATE_H
#define __WIFI_UPDATE_H

/*
 * "Upgrade WiFi module" flow. A blocking, full-screen modal (like launching a
 * game): searches the SD card for the ESP firmware image, flashes it to the
 * ESP-01 with an on-screen progress bar, then waits for Special Button 2 before
 * returning to the settings menu. Invoked as a settings ACTION leaf.
 */
void wifiUpdateRun(void);

/*
 * "Test WiFi module" flow. Resets the ESP, listens on USART1 at the runtime
 * baud, and shows its UART heartbeat (received byte count + last line) on
 * screen — a quick "is the ESP alive?" check, independent of the on-board LED.
 * Blocking; returns on Special Button 2.
 */
void wifiTestRun(void);

#endif /* __WIFI_UPDATE_H */
