#ifndef __WIFI_UPDATE_H
#define __WIFI_UPDATE_H

/*
 * "Upgrade WiFi module" flow. A blocking, full-screen modal (like launching a
 * game): searches the SD card for the ESP firmware image, flashes it to the
 * ESP-01 with an on-screen progress bar, then waits for Special Button 2 before
 * returning to the settings menu. Invoked as a settings ACTION leaf.
 */
void wifiUpdateRun(void);

#endif /* __WIFI_UPDATE_H */
