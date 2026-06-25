/*
 * ESP-01S firmware — GameConsole WiFi module.
 *
 * Acts as the slave of the console<->ESP frame protocol (network_protocol.h):
 * the STM32 sends commands, this firmware scans/joins WiFi and performs HTTP
 * GETs, relaying the body back in chunks. That lets the console pull games and
 * its own ESP firmware from the PC update server. It also carries ESP-NOW
 * console-to-console multiplayer (NP_CMD_MP_*).
 *
 * This file is just the entry points: setup() brings the radio up and loop()
 * reads one command and dispatches it to a subsystem. The handlers live in:
 *   protocol.{h,cpp}     framing (np::)
 *   wifi.{h,cpp}         scan / connect / status / disconnect
 *   http.{h,cpp}         HTTP open / read / close
 *   espnow_link.{h,cpp}  ESP-NOW multiplayer (MP begin / end / service)
 *
 * The console flashes this image via Settings -> "Upgrade WiFi module" (it reads
 * ESP01.bin off the SD card; see ../docu/flasher.md).
 */
#include <Arduino.h>

extern "C"
{
#include "user_interface.h" /* rst_info / REASON_EXCEPTION_RST for the boot banner */
}

#include "network_protocol.h"
#include "protocol.h"
#include "wifi.h"
#include "http.h"
#include "espnow_link.h"

void setup()
{
    Serial.setRxBufferSize(1024);
    Serial.begin(NETWORK_UART_BAUD);

    /* Boot banner with the reset cause. If this appears *during* a command (while
     * the console is reading the link), it proves the ESP reset mid-operation. A
     * brownout/power glitch reads as "Power On"/"External System"; a firmware
     * crash reads as "Exception" — and there we dump the Xtensa exception cause +
     * faulting PC so the crash names itself (cause 28/29 = bad pointer / overflow,
     * 9 = unaligned, 0 = illegal instr) instead of just "Exception". */
    /* Build stamp so the running firmware is identifiable in the SWO log (compare
     * against when you last `make esp` + flashed). __DATE__/__TIME__ are filled by
     * the compiler, so every build is uniquely tagged without manual version bumps. */
    np::logf(NP_LOG_INFO, "ESP firmware: proto v%u, built " __DATE__ " " __TIME__,
             (unsigned)NETWORK_PROTOCOL_VERSION);

    const struct rst_info *ri = ESP.getResetInfoPtr();
    if (ri != nullptr && ri->reason == REASON_EXCEPTION_RST)
    {
        np::logf(NP_LOG_WARN, "ESP boot - proto v%u, reset: Exception cause=%u epc1=0x%08x excvaddr=0x%08x",
                 (unsigned)NETWORK_PROTOCOL_VERSION, (unsigned)ri->exccause,
                 (unsigned)ri->epc1, (unsigned)ri->excvaddr);
    }
    else
    {
        np::logf(NP_LOG_INFO, "ESP boot - proto v%u, reset: %s",
                 (unsigned)NETWORK_PROTOCOL_VERSION, ESP.getResetReason().c_str());
    }

    wifiInit();
}

void loop()
{
    uint8_t type;
    uint16_t len;

    /* Block for a command (long timeout so we just wait for the master). */
    if (!np::readFrame(&type, &len, 60000u))
    {
        return;
    }

    switch (type)
    {
    case NP_CMD_PING:
    {
        /* Echo the protocol version so the console can verify a matching build. */
        const uint8_t ver = NETWORK_PROTOCOL_VERSION;
        np::sendFrame(NP_RSP_PONG, &ver, 1u);
        break;
    }
    case NP_CMD_SCAN:
        wifiHandleScan();
        break;
    case NP_CMD_CONNECT:
        wifiHandleConnect(len);
        break;
    case NP_CMD_STATUS:
        wifiSendStatus();
        break;
    case NP_CMD_HTTP_OPEN:
        httpHandleOpen(len);
        break;
    case NP_CMD_HTTP_READ:
        httpHandleRead(len);
        break;
    case NP_CMD_HTTP_CLOSE:
        httpHandleClose();
        break;
    case NP_CMD_DISCONNECT:
        wifiHandleDisconnect();
        break;
    case NP_CMD_MP_BEGIN:
        espnowHandleBegin(len);
        break;
    case NP_CMD_MP_END:
        espnowHandleEnd();
        break;
    case NP_CMD_MP_SERVICE:
        espnowHandleService(len);
        break;
    default:
        np::logf(NP_LOG_WARN, "unknown cmd 0x%02X", type);
        np::sendError(0xFF);
        break;
    }
}
