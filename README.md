# GameConsole

An embedded game console platform for the STM32F407VET6, with a NES-inspired tile renderer, SD card game loader, and a shared ConsoleAPI bridge that lets game binaries call into the firmware without linking against it.

**Development board:** [STM32F407VET6 F4VE V2.0](https://stm32-base.org/boards/STM32F407VET6-STM32-F4VE-V2.0.html)

![Console PCB](docu/HW/PCB_DESIGN.png)

## Quick Start

Requires `arm-none-eabi-gcc` and `openocd` on PATH.

```bash
make all       # build Console firmware + GameXO game
make flash     # flash firmware via OpenOCD/STLink
make flashswo  # flash and start SWO trace output
make deploy    # copy GameXO.bin to SD card at /mnt/sd
make clean     # remove all build artifacts
```

## Documentation

| Topic                    | Location                                         |
| ------------------------ | ------------------------------------------------ |
| API & renderer internals | [docu/API_README.md](docu/API_README.md)         |
| Hardware & pinout        | [docu/HW.md](docu/HW.md)                        |
| Game creation guide      | [docu/game_creation.md](docu/game_creation.md)  |
| Pixel Forge (graphics)   | [tools/graphics/README.md](tools/graphics/README.md) |
| Music Creator tool       | [tools/music_creator/README.md](tools/music_creator/README.md) |

## Naming Conventions

| Element            | Convention       | Example                  |
| ------------------ | ---------------- | ------------------------ |
| Macros / Defines   | UPPER_SNAKE_CASE | `MAX_BUFFER_SIZE`        |
| Constants          | UPPER_SNAKE_CASE | `DEFAULT_TIMEOUT`        |
| Global variables   | g_snake_case     | `g_system_initialized`   |
| Static globals     | s_snake_case     | `s_buffer_index`         |
| Local variables    | snake_case       | `temp_value`             |
| Functions          | snake_case       | `init_peripherals()`     |
| Struct / Enum types| PascalCase       | `SensorData`, `PowerState` |
| Struct members     | snake_case       | `adc_value`              |
| Typedefs           | PascalCase       | `Byte`                   |
