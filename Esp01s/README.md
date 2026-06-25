# ESP-01S Firmware (`Esp01s/`)

PlatformIO project for the console's **ESP-01S** WiFi module (ESP8266EX, 1 MB
flash). It builds the firmware image the console flashes onto the ESP from the
SD card via **Settings → "Upgrade WiFi module"**.

This is an independent build target — a different MCU and toolchain from the
STM32 `Console/` firmware — which is why it sits at the repo root alongside
`Console/` and the games under `Apps/` rather than inside the console source tree.

> **What it does:** the slave side of the framed console↔ESP protocol
> (the `src/` modules + the shared `../Shared/Esp01s/network_protocol.h`), in two
> modes: **WiFi pull** — scan / connect / HTTP-GET, so the console can download
> games and its own firmware — and **ESP-NOW local multiplayer**, console-to-console
> play for up to four consoles. The multiplayer stack (this firmware's `MP_*`
> handlers, the session protocol, and the diagrams) is documented in
> [`../docu/espnow.md`](../docu/espnow.md).

## Layout

| Path | What |
| ---- | ---- |
| `platformio.ini` | board (`esp01_1m`), framework (`arduino`), build flags |
| `src/main.cpp` | entry points — `setup()` (radio bring-up) + `loop()` (read one command, dispatch) |
| `src/protocol.{h,cpp}` | framing layer (`np::`): send/read framed packets, owns the TX/RX buffers |
| `src/wifi.{h,cpp}` | scan / connect / status / disconnect + radio regulatory bring-up |
| `src/http.{h,cpp}` | HTTP GET open / read / close (the download path) |
| `src/espnow_link.{h,cpp}` | ESP-NOW multiplayer transport (`MP_*` — begin / end / service) |
| `Makefile` | `build` (PlatformIO) + `deploy` (copy firmware to SD as `ESP01.bin`) |
| `../Shared/Esp01s/network_protocol.h` | the console↔ESP wire contract, **shared with the console firmware** |

The same `network_protocol.h` is on the console's include path
(`-I../Shared/Esp01s`), so both this firmware and `Console/Src/Network` build
against one source of truth (baud, protocol version, command IDs).

## Build

From the repo root:

```bash
make esp           # builds via PlatformIO -> .pio/build/esp01s/firmware.bin
```

(Equivalent to `pio run` inside this directory, or the PlatformIO VS Code
extension. If `pio` isn't on PATH, override it: `make esp PIO=/path/to/pio`.)

## Deploy

`make deploy` (from the repo root) stages the built firmware into the
update-server content tree as `tools/update_server/content/wifi/ESP01.bin` (the
name the flasher looks for), alongside the game files:

```bash
make esp      # build the firmware first
make deploy   # stage it (+ the game) into the update server tree
```

(The ESP step skips quietly if the firmware hasn't been built, so it never
breaks the game deploy.)

From there the console gets it one of two ways:
- **Now (manual):** copy `ESP01.bin` onto the SD-card root yourself, then on the
  console: **Settings → Upgrade WiFi module**.
- **Later (over WiFi):** the console pulls it from the update server (see
  [`../tools/update_server`](../tools/update_server)).

Either way the console drives the ESP into its ROM bootloader and flashes the
image, showing a progress bar. See [`../docu/flasher.md`](../docu/flasher.md) for
the flashing internals.

> Deploy is manual today. The plan is a PC-side server holding these images,
> which the console pulls over WiFi once the runtime link exists.

## Notes

- **Uploading directly** from PlatformIO (`pio run -t upload`) is *not* the
  normal path here — the console is the flasher. Direct upload would need a
  USB-serial adapter wired to the ESP and the board strapped into bootloader
  mode manually. The board id is still required for the correct flash
  layout/size.
- **LED pin:** the on-board ESP-01S LED is on **GPIO2** and is **active-low**
  (drive LOW = on). Do *not* use `LED_BUILTIN` — for this board's `generic`
  variant it maps to GPIO1, which is also UART0 TX (owned by `Serial`), so it
  matches neither the physical LED nor a free pin. `wifi.cpp` drives GPIO2
  directly (it's the WiFi-status light).
- **Heartbeat / aliveness check:** the firmware prints `blink N` once per second
  on `Serial` (UART0, 921600). That TX line is wired to the console's USART1, so
  **Settings → Test WiFi module** shows the received byte count and last line —
  use it to confirm the ESP is running even if the LED is dead or miswired.
