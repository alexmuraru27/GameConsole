# GameConsole

An embedded game console platform for the STM32F407VET6, with a [scanline sprite compositor](docu/renderer.md), SD card game loader, and a syscall-based ConsoleAPI that lets game binaries call into the firmware without linking against it.

**Development board:** [STM32F407VET6 F4VE V2.0](https://stm32-base.org/boards/STM32F407VET6-STM32-F4VE-V2.0.html)

![Console PCB](docu/HW/PCB_DESIGN.png)

## Quick Start

Requires `arm-none-eabi-gcc` and `openocd` on PATH.

```bash
make all       # build Console firmware + the Apps (GameXO + TestRenderer + TestBuzzer)
make flash     # flash firmware via OpenOCD/STLink
make flashswo  # flash and start SWO trace output
make deploy    # stage the apps' .bin/.pak (+ OS/ESP images) into the update-server tree
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
