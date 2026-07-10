# GameConsole

An embedded game console platform for the STM32F407VET6, with a [scanline sprite compositor](docu/renderer.md), SD card game loader, and a syscall-based ConsoleAPI that lets game binaries call into the firmware without linking against it.

**Development board:** [STM32F407VET6 F4VE V2.0](https://stm32-base.org/boards/STM32F407VET6-STM32-F4VE-V2.0.html)

![Console PCB](docu/HW/PCB_DESIGN.png)

## Prerequisites

Building and flashing the console firmware needs an ARM cross-toolchain, `make`, and
OpenOCD on `PATH`. The ESP-01 WiFi firmware and the Python tools are optional — needed
only for those paths. The versions below are what the project is developed and tested
against; at or above the listed minimum should work.

| Tool | Needed for | Minimum | Tested |
| ---- | ---------- | ------- | ------ |
| `arm-none-eabi-gcc` — Arm GNU Toolchain (incl. `newlib-nano`) | Console / Bootloader / Apps builds | 10 | 15.2.Rel1 |
| GNU `make` | every build | 4.0 | 4.3 |
| `openocd` — with ST-Link support | `make flash` / `make flashswo` (flash + SWO trace) | 0.11 | 0.12.0 |
| Python | any of the repo's Python tools | 3.10 | 3.12.3 |
| PlatformIO Core (`pio`) | `make esp` (ESP-01 firmware) **only** | 6.0 | 6.1.19 |
| `bear` | `make refreshcompilecommands` — editor tooling **only** | 3.0 | 3.1.3 |

- **ARM toolchain.** The Makefiles invoke `arm-none-eabi-gcc` / `objcopy` / `size` from
  `PATH`; if the toolchain lives elsewhere, pass `GCC_PATH=<dir>` (see `common.mk`).
  `newlib-nano` (`-specs=nano.specs`) ships with the Arm GNU Toolchain.
- **PlatformIO must be 6.x, not the distro 4.x.** PlatformIO 4.x calls a Click API
  removed in Click 8, so `pio run` crashes on a current system. Install the modern Core
  with the [official installer](https://docs.platformio.org/en/latest/core/installation/)
  or `pipx install platformio`; it self-manages the `espressif8266` platform and the
  xtensa toolchain on first build. `Esp01s/Makefile` auto-prefers the Core at
  `~/.platformio/penv/bin/pio`, or set `PIO=<path>`.
- **Python tools.** The Asset Packer, Memory Analysis, and Update Server are
  **standard-library only** — no `pip install` needed. The two PyQt6 desktop apps need
  extra packages:

  | Package | Tested | Used by |
  | ------- | ------ | ------- |
  | PyQt6 | 6.6.1 (Qt 6.4.2) | Pixel Forge, Music Creator |
  | pygame | 2.5.2 | Music Creator (audio preview) |
  | numpy | 1.26.4 | Music Creator |
  | PyYAML | 6.0.1 | Pixel Forge, Music Creator, Asset Packer (manifests) |

  Install with `pip install PyQt6 pygame numpy PyYAML`.
- **Editor integration (optional).** `make refreshcompilecommands` does a clean rebuild
  under [`bear`](https://github.com/rizsotto/Bear) to regenerate `compile_commands.json`,
  which the **clangd** language server (usually your editor's clangd extension, so not a
  separate CLI install) reads for cross-file navigation, completion, and diagnostics.
  Not needed to build or flash — only for editor tooling.

## Quick Start

Requires the ARM toolchain, `make`, and `openocd` on `PATH` (see [Prerequisites](#prerequisites)).

```bash
make all       # build Console firmware + the Apps (GameXO + TestRenderer + TestBuzzer)
make flash     # flash firmware via OpenOCD/STLink
make flashswo  # flash and start SWO trace output
make deploy    # stage the apps' .bin/.pak (+ OS/ESP images) into the update-server tree
make refreshcompilecommands  # clean rebuild under bear -> regenerate compile_commands.json (clangd)
make clean     # remove all build artifacts
```

## Documentation

| Topic                    | Location                                                           |
| ------------------------ | ------------------------------------------------------------------ |
| API & renderer internals | [docu/API_README.md](docu/API_README.md)                           |
| Kernel & game isolation  | [docu/kernel.md](docu/kernel.md)                                   |
| Renderer deep-dive       | [docu/renderer.md](docu/renderer.md)                               |
| Memory layout & EEPROM   | [docu/memory.md](docu/memory.md)                                   |
| ESP-NOW multiplayer      | [docu/espnow.md](docu/espnow.md)                                   |
| Hardware & pinout        | [docu/HW.md](docu/HW.md)                                           |
| Game creation & API      | [docu/game_template/README.md](docu/game_template/README.md)       |
| Pixel Forge (graphics)   | [tools/graphics/README.md](tools/graphics/README.md)               |
| Music Creator tool       | [tools/music_creator/README.md](tools/music_creator/README.md)     |
| Asset Packer tool        | [tools/packer/README.md](tools/packer/README.md)                   |
| Memory Analysis tool     | [tools/memory_analysis/README.md](tools/memory_analysis/README.md) |

## Naming Conventions

| Element             | Convention       | Example                    |
| ------------------- | ---------------- | -------------------------- |
| Macros / Defines    | UPPER_SNAKE_CASE | `MAX_BUFFER_SIZE`          |
| Constants           | UPPER_SNAKE_CASE | `DEFAULT_TIMEOUT`          |
| Global variables    | g_snake_case     | `g_system_initialized`     |
| Static globals      | s_snake_case     | `s_buffer_index`           |
| Local variables     | snake_case       | `temp_value`               |
| Functions           | camelCase        | `initPeripherals()`        |
| Struct / Enum types | PascalCase       | `SensorData`, `PowerState` |
| Struct members      | snake_case       | `adc_value`                |
| Typedefs            | PascalCase       | `Byte`                     |
