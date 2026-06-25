#pragma once

/*
 * HTTP GET subsystem for the ESP-01S firmware. The console opens a GET, reads
 * the body back in chunks, and closes it — that's how it pulls games and
 * firmware from the update server. One session at a time. See main.cpp for
 * dispatch.
 */

#include <Arduino.h>

void httpHandleOpen(uint16_t len);
void httpHandleRead(uint16_t len);
void httpHandleClose(void);
