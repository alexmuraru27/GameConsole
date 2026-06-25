#pragma once

/*
 * WiFi station subsystem for the ESP-01S firmware: bring-up (radio mode,
 * regulatory country, TX power, power saving, status LED) plus the scan /
 * connect / status / disconnect command handlers. See main.cpp for dispatch.
 */

#include <Arduino.h>

/* One-time WiFi/radio bring-up; call from setup() after the boot banner. */
void wifiInit(void);

/* Command handlers (dispatched from loop()). */
void wifiHandleScan(void);
void wifiHandleConnect(uint16_t len);
void wifiSendStatus(void);
void wifiHandleDisconnect(void);
