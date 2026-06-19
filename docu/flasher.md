# The ESP Flasher

This document is the ground-up explanation of how the console reflashes its
ESP-01 WiFi module from the SD card. It covers *what* the feature is, *why* it
is built the way it is, and every moving part from the menu entry down to the
bytes on the wire. If you have never seen this code before, read it top to
bottom.

- **Glue source:** [`Console/Src/Flasher/esp_flasher.c`](../Console/Src/Flasher/esp_flasher.c),
  [`esp_flasher_port.c`](../Console/Src/Flasher/esp_flasher_port.c) (+ headers in `Console/Inc/Flasher/`)
- **UART driver:** [`Console/Src/Peripherals/usart.c`](../Console/Src/Peripherals/usart.c)
- **Bootstrap pins:** [`Console/Src/Peripherals/gpio.c`](../Console/Src/Peripherals/gpio.c) (`esp01Set*`)
- **UI flow:** [`Console/Src/MainMenu/wifi_update.c`](../Console/Src/MainMenu/wifi_update.c)
- **Vendored library:** [`tools/esp-serial-flasher`](../tools/esp-serial-flasher) (git submodule, v2.0.0)
- **Hardware:** STM32F407VET6 host ↔ ESP-01 (ESP8266) on USART1

---

## 1. What it does

The ESP-01 ships with its own flashable firmware (the WiFi co-processor sketch).
Rather than pulling the module and wiring up a USB-serial adapter, the console
reflashes it **in place**: a user drops `ESP01.bin` on the SD card, opens
**Settings → Upgrade WiFi module**, and the STM32 plays the role of the host
flasher — driving the ESP into its ROM download mode and streaming the new image
over USART1, with a progress bar on screen.

> **Scope.** This is the *flashing pipeline* only. Producing `ESP01.bin` (an
> Arduino/PlatformIO build of the ESP firmware) and the runtime network protocol
> over USART1 are separate, later work. The `network.c` stack is still a stub.

---

## 2. The big picture

```
  Settings tree (SETTING_ACTION leaf)
        │  wifiUpdateRun()
        ▼
  MainMenu/wifi_update.c ───── on-screen progress bar (renderer)
        │  espFlasherFlashFile("ESP01.bin", cb, ctx)
        ▼
  Flasher/esp_flasher.c ────── FatFs: streams the image in 1 KB blocks
        │  esp_loader_*()           (esp_loader_connect_with_stub, flash_start/write/finish)
        ▼
  tools/esp-serial-flasher  ── the SLIP/ESP ROM protocol (vendored, HAL-free core)
        │  port->ops->{write,read,enter_bootloader,...}
        ▼
  Flasher/esp_flasher_port.c ─ bare-metal vtable
        │
        ├── usart.c   (polled USART1 byte I/O, timeouts)
        └── gpio.c    (EN / RST / IO0 / IO2 bootstrap pins)
```

Two ideas drive the design:

1. **Reuse Espressif's protocol code, don't reimplement it.** The ESP ROM
   download protocol (SLIP framing, sync, flash-begin/data/end, MD5 verify, the
   RAM stub upload) is fiddly and well-tested upstream. It lives as a git
   submodule and we compile its **HAL-free core** straight into the firmware.
2. **Bridge it to our bare-metal drivers, not to the HAL.** The library talks to
   hardware exclusively through a small function-pointer **vtable**
   (`esp_loader_port_ops_t`). We implement that vtable against the project's own
   register-level USART1 and GPIO — so no ST HAL is ever pulled in.

---

## 3. Why a custom port (and not the bundled one)

The submodule ships `port/stm32_port.c`, but it is written against the STM32
**HAL** (`HAL_UART_Transmit`, `HAL_GPIO_WritePin`, `UART_HandleTypeDef`, …). This
firmware is register-level throughout and has no HAL. Pulling in the HAL just to
satisfy one port file would bloat the build and duplicate drivers we already
have.

So `stm32_port.c` is **deliberately not compiled**. Instead
`esp_flasher_port.c` provides the vtable. The library's port interface
(`include/esp_loader_io.h`) is a clean inheritance-by-embedding design — every
callback receives the `esp_loader_port_t*` base and recovers the concrete struct
with `container_of`:

```c
typedef struct {
    esp_loader_port_t base;   /* embedded handle the library sees */
    uint32_t          time_end; /* deadline for the one-shot timer */
} EspFlasherPort;
```

### The vtable mapping

| `esp_loader_port_ops_t` callback | Our implementation |
| -------------------------------- | ------------------ |
| `init`                           | `usartInit()` — (re)configure USART1 |
| `write` / `read`                 | `usartWriteBytes()` / per-byte `usartReadByte()`, honoring the timeout |
| `enter_bootloader`               | IO0 low → pulse RST → release (see §4) |
| `reset_target`                   | IO0 high → pulse RST (normal boot) |
| `start_timer` / `remaining_time` | deadline math over `getSysTime()` (1 ms SysTick) |
| `delay_ms`                       | `delay()` (SysTick busy-wait) |
| `change_transmission_rate`       | `usartSetBaud()` |
| `log` / `log_hex`                | `NULL` — the library stays silent; we narrate at the high level |
| `spi_*` / `sdio_*`               | `NULL` — serial (UART) transport only |

---

## 4. Hardware: pins and the bootstrap dance

The ESP-01 link and its strap pins (see [`docu/HW.md`](HW.md)):

| Signal     | STM32 pin | Direction | Role in flashing |
| ---------- | --------- | --------- | ---------------- |
| USART1 TX  | PA9 (AF7) | host→ESP  | command/data |
| USART1 RX  | PA10 (AF7)| ESP→host  | responses |
| EN / CH_PD | PB10      | output    | held **high** = chip enabled |
| RST        | PB6       | output    | pulsed **low** to reset |
| IO0 / GPIO0| PC6       | **Hi-Z**, driven low only during entry | **low at reset** = ROM bootloader |
| IO2 / GPIO2| PC13      | **Hi-Z** (never driven) | module pull-up holds high at reset |

> **IO0/IO2 are functional GPIOs of the *running* ESP firmware** (the ESP-01S
> on-board LED is on GPIO2). The console must not hold them once flashing is
> done — they idle as **inputs (Hi-Z)** and the module's pull-ups keep them high
> for a normal boot. `esp01SetBootloader` drives IO0 low (output) only for the
> bootloader-entry pulse, then releases it back to Hi-Z; IO2 is never driven, so
> the firmware owns the LED. Holding either as a push-pull output would fight the
> firmware (a freshly-flashed blinky wouldn't blink).

Pin knowledge lives in `gpio.c`, which exposes three intention-named helpers so
the flasher never pokes registers directly:

```c
void esp01SetEnable(bool enabled);   /* EN  high = enabled            */
void esp01SetReset(bool in_reset);   /* RST low  = held in reset      */
void esp01SetBootloader(bool enter); /* IO0 low  = ROM download mode  */
```

The ESP8266 samples GPIO0 **at the moment reset is released**. Low → it enters
the serial download loader; high → it boots normally. `enter_bootloader` times
this out (idle high, holds in ms):

```
EN  ─────────────────────────────────  (high throughout)
IO0 ──┐                          ┌────  low while reset releases → bootloader
      └──────────────────────────┘
RST ──────┐              ┌─────────────  pulse low to reset
          └──────────────┘
          │←ESP_RESET_HOLD→│←BOOT_HOLD→│
              (100 ms)        (50 ms)
```

`reset_target` is the inverse (IO0 high, pulse RST) and is used once at the end
to boot the freshly-flashed firmware.

---

## 5. The USART1 driver

`usart.c` was a no-op stub; it is now a small polled 8N1 driver. USART1 is on
**APB2 (PCLK2 = SYSCLK/2 = 84 MHz)**. With 16× oversampling the baud register is
simply `BRR = PCLK2 / baud` (rounded), because that encoding already packs the
12.4 fixed-point `USARTDIV`:

```c
USART1->BRR = (USART1_PCLK_HZ + baud / 2U) / baud;   /* 84 MHz / 115200 ≈ 729 */
```

The clock is enabled centrally in `peripheralsClockEnable()`
(`sysclock.c`) alongside the other peripherals; the pins are set up in
`initGpioEsp01()`. I/O is blocking with a millisecond deadline derived from
`getSysTime()`:

- `usartWriteBytes()` spins on `TXE` per byte, then waits for `TC` so a following
  strap-pin toggle can't truncate the last frame.
- `usartReadByte()` spins on `RXNE`, returning the byte or `-1` on timeout. The
  port's `read` charges each byte the remaining budget of the whole-read timeout.

Polling (not DMA/interrupts) is the right call here: flashing is a one-shot modal
operation, the frames are short, and the ESP only talks in response to our
commands — there is no free-running stream to keep up with.

---

## 6. The flashing flow

`espFlasherFlashFile(path, cb, ctx)` in `esp_flasher.c` is the whole operation,
mirroring the upstream `examples/common/example_common.c` but **streaming from
the SD card** instead of a memory blob:

1. `f_open(path)` via FatFs; read the size.
2. **Align up** the image size to 4 bytes (`(size + 3) & ~3`) — the bootloader
   requires a 4-byte-aligned image. The tail of the final block is padded with
   `0xFF`. The host and target both MD5 the padded image, so verification stays
   consistent.
3. `esp_loader_init_serial(&loader, espFlasherPortGet())`.
4. `esp_loader_connect_with_stub()` — sync with the ROM, then upload and run the
   **ESP8266 RAM stub** (faster erase/write). Connection params come from
   `ESP_LOADER_CONNECT_DEFAULT()` (100 ms sync timeout, 10 trials). The link runs
   at **115200**; the upstream convention skips the high-rate switch for ESP8266.
5. `esp_loader_flash_start()` at **offset 0** (an ESP8266 Arduino image is a
   single blob from address 0). This also erases the region — it can take a few
   seconds with no callback yet, which is why the UI shows a "Connecting…" screen
   before the loop.
6. Loop: `f_read` a 1 KB block → pad if short → `esp_loader_flash_write()` →
   invoke the progress callback `cb(done, total, ctx)`.
7. `esp_loader_flash_finish()` — finalizes and (since `skip_verify = false`)
   checks the accumulated MD5 against the device.
8. `esp_loader_reset_target()` — boots the new firmware.

Outcomes collapse to a small status enum the UI can render:

```c
ESP_FLASH_OK | NO_FILE | CONNECT_FAIL | WRITE_FAIL | VERIFY_FAIL
```

A single static 1 KB `s_payload` buffer is reused for every block (flashing is
strictly sequential).

---

## 7. The on-screen flow

`wifi_update.c` is a **blocking modal** — the same pattern as launching a game
(`game_list.c` hands the renderer to the game, then rebuilds its surface when it
returns). `wifiUpdateRun()`:

1. Checks `loaderMediaPresent()` and `f_stat("ESP01.bin")`; if missing, shows
   *"ESP01.bin not found on SD"* and waits for **Special Button 2**.
2. Draws *"Connecting to ESP…"*, then calls `espFlasherFlashFile()` with a
   progress callback that redraws a bar + percentage each block (it reuses the
   theme's `menuDrawBar()`, stacked into a thicker bar).
3. On success it deletes `ESP01.bin` from the card (best-effort `f_unlink` — the
   SD stack is read-write), shows *"Update complete!"* (success chime), and waits
   for Special Button 2. On failure it shows *"Failed: …"* (error chime) and
   leaves the image in place so you can retry.

The image file (`ESP_FIRMWARE_FILENAME` in `esp_flasher.h`) shares the `.bin`
extension with games and isn't filtered out of the **Games** list, but it's
harmless there: the game loader rejects it at launch via the `GAME_BINARY_MAGIC`
/ ABI check before any code runs.

It is wired into the settings tree via the `SETTING_ACTION` leaf kind in
`settings_menu.c`:

```c
{ .label = "Upgrade WiFi module", .kind = SETTING_ACTION, .action = wifiUpdateRun },
```

When entered, the menu calls `item->action()` (which blocks for the flash's
lifetime) and then `menuResetSurface()` to restore the settings screen.

A sibling action, **Test WiFi module** (`wifiTestRun()` in the same module),
resets the ESP, switches USART1 to the runtime baud (921600), and listens to the
ESP's UART heartbeat — showing the received byte count and last line on screen.
It's the quick "is the ESP actually running?" check, independent of the on-board
LED (which on the ESP-01S is GPIO2; see `Esp01s/README.md`).

> Rendering happens *between* `flash_write` calls, never during one, so it can't
> disturb protocol timing. The buzzer/joystick ISRs keep running throughout; the
> success/failure chimes play only after flashing finishes.

---

## 8. Build integration

The Makefile compiles the HAL-free core of the submodule directly — no separate
library build:

- `ESP_FLASHER_DIR = ../tools/esp-serial-flasher`
- Core sources added to `C_SOURCES`: `esp_loader.c`, `esp_targets.c`,
  `md5_hash.c`, `protocol_serial.c`, `protocol_uart.c`, `slip.c`, and
  `$(wildcard $(ESP_FLASHER_DIR)/src/stubs/*.c)`.
- Includes: `-I$(ESP_FLASHER_DIR)/include -I$(ESP_FLASHER_DIR)/private_include`
  and our `-IInc/Flasher`.

**The stub blobs.** `connect_with_stub` indexes a runtime table
(`esp_stubs_table.c`) of *all* supported targets, so the linker can't prove which
entries are unused and keeps every stub blob (~91 KB: ~15 KB ESP8266 + ~76 KB
unused ESP32 family). That is harmless here — flash sits at ~29% of 512 KB — and
it buys the faster stub-based flashing path. If flash ever gets tight, switching
to plain `esp_loader_connect` (no stub) lets all the stub `.c` files drop out.

No mandatory `-D` config is needed: `SERIAL_FLASHER_WRITE_BLOCK_RETRIES`
self-defaults, and the reset/boot-hold/invert macros are only referenced by the
HAL port we don't compile.

---

## 9. Logging

The subsystem logs on the **`LOGGER_FLASHER`** channel (printed `[FLAS]`):
USART bring-up, the requested update, connect + detected target, flash
start/finish, and every error path — but **never** inside the read/write loops
(one SWO line is ~5 µs/byte and would perturb the link). The library's own
`log`/`log_hex` callbacks are `NULL`, so all narration comes from our side.

---

## 10. Using it / verifying on hardware

1. Build the [`Esp01s/`](../Esp01s) PlatformIO project (`pio run`) — the full
   image, flashed from address 0.
2. Copy it to the **root** of the SD card as `ESP01.bin` via `make -C Esp01s
   deploy` (or top-level `make deploy`).
3. `make flashswo` to flash the console and watch SWO.
4. On the device: **Settings → Upgrade WiFi module**.
5. The `[FLAS]` log should narrate connect → erase → per-block progress → MD5
   verified, and the on-screen bar should reach 100% then *"Update complete!"*.

### Troubleshooting

| Symptom | Likely cause |
| ------- | ------------ |
| `CONNECT_FAIL` / "ESP not responding" | wiring/strap pins, ESP not powered, or it didn't enter the loader — check EN high and the RST/IO0 pulse |
| `VERIFY_FAIL` | corrupt image on the card, or line noise during transfer (shorten wires / lower baud) |
| Garbled reads mid-transfer | UART overrun — the polled reader fell behind; verify PCLK2/baud and that no long blocking work sneaks between reads |
| "ESP01.bin not found" | file missing, misnamed, or card not mounted |
