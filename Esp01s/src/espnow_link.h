#pragma once

/*
 * ESP-NOW multiplayer transport for the ESP-01S firmware (NP_CMD_MP_*).
 *
 * The ESP is a dumb byte mover for console-to-console play: it sends/receives
 * raw ESP-NOW packets and reports each inbound packet's source MAC. ALL session
 * logic (discovery, peer indices, heartbeat, roster) lives in the console
 * (mp_session.c). MP_BEGIN/MP_END bracket the ESP-NOW mode; it never overlaps
 * the HTTP path (you either poll updates or play). See ../docu/espnow.md.
 *
 * Named espnow_link (not espnow) so this header never shadows the SDK's
 * <espnow.h>, which espnow_link.cpp includes.
 */

#include <Arduino.h>

void espnowHandleBegin(uint16_t len);
void espnowHandleEnd(void);
void espnowHandleService(uint16_t len);
