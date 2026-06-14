# GameConsole: Console & API Documentation

## Overview
GameConsole is a modular embedded game console platform based on the STM32F407 microcontroller. It provides a hardware abstraction layer and a rich API for game development, including graphics rendering, joystick input, sound, and asset management.

---

## Table of Contents
- [Architecture](#architecture)
- [Main Loop](#main-loop)
- [API Overview](#api-overview)
  - [System Time](#system-time)
  - [Debug/USART](#debugusart)
  - [Sound (Buzzer)](#sound-buzzer)
  - [Joystick](#joystick)
  - [Renderer](#renderer)
  - [Asset Loader](#asset-loader)
- [Module Internals](#module-internals)
  - [System/Startup](#systemstartup)
  - [Renderer](#renderer-internal)
  - [Joystick](#joystick-internal)
  - [Buzzer](#buzzer-internal)
  - [Asset Loader](#asset-loader-internal)
  - [SDIO & Filesystem](#sdio--filesystem)
  - [Interrupts & Syscalls](#interrupts--syscalls)
- [Initialization Sequence](#initialization-sequence)
- [Example Usage](#example-usage)
- [File Structure](#file-structure)
- [See Also](#see-also)
- [License](#license)

---

## Architecture
- **MCU:** STM32F407
- **Main Components:**
  - Renderer (graphics)
  - Joystick (input)
  - Buzzer (sound)
  - Asset Loader (game data)
  - SDIO (storage)
  - USART (debug)

All modules are initialized in `gameConsoleInit()`.

---

## Main Loop
The main application loop (see `main.c`) follows this structure:

```c
int main(void) {
    gameConsoleInit();
    // ... (test/init code)
    while (1) {
        update();   // Handle input, update game state
        render();   // Draw frame
        syncFrame(); // Maintain FPS
    }
}
```

- **update()**: Reads joystick state, updates sprite positions.
- **render()**: Renders the frame, handles sound triggers.
- **syncFrame()**: Ensures a fixed frame rate (default 50 FPS).

---

## API Overview
The Console API is exposed via a `ConsoleAPI` struct, making it accessible to loaded games and modules. Below is a summary of the main API groups and their functions.

### System Time
- `getSysTime()`: Returns system uptime in ms.
- `delay(ms)`: Busy-wait delay.


### Sound (Buzzer)
- `buzzerGetMaxTracks()`: Get number of sound tracks.
- `buzzerPlay(track, data, size)`: Play sound data.
- `buzzerPlayWithFlag(...)`: Play with callback.
- `buzzerPause(track)`, `buzzerResume(track)`, `buzzerStop(track)`: Control playback.

### Joystick
- `joystickGetRBtnUp/Down/Left/Right()`: Right stick buttons.
- `joystickGetLBtnUp/Down/Left/Right()`: Left stick buttons.
- `joystickGetSpecialBtn1/2()`: Special buttons.
- `joystickGetRAnalogX/Y()`, `joystickGetLAnalogX/Y()`: Analog axes.

### Renderer
- `rendererRender()`: Draw the current frame.

### Asset Loader
- `assetLoaderGetAssetMetadata(asset_id, *size)`: Get asset size.
- `assetLoaderGetAssetData(asset_id, *buffer)`: Load asset data.
- `assetLoaderGetAssetHeader(*header)`: Get asset metadata.

---

## Module Internals

### System/Startup
- **SystemInit**: Configures system clock and core peripherals.
- **syscalls.c**: Implements minimal newlib system calls for embedded C runtime (e.g., `_write`, `_read`, `_exit`).
- **stm32f4xx_it.c**: Defines interrupt handlers for faults and system exceptions (NMI, HardFault, etc.).

### Renderer (renderer.c/h)
- Scanline-based graphics engine rendering to the ILI9341 display.
- 320×240 pixel screen, double-buffered scanline approach for DMA-friendly rendering.
- 64-color system palette (RGB565).
- Internal functions:
  - `rendererInit()`: Initialize scanline buffers.
  - `rendererRender()`: Draws the current frame (currently a stub — rendering is being reworked).
  - `rendererGetWidthPixels()`, `rendererGetHeightPixels()`: Query screen dimensions.

### Joystick (joystick.c/h)
- Reads analog and digital joystick inputs via ADC and GPIO.
- Debounces and interprets button presses and analog stick positions.
- Internal functions:
  - `joystickInit()`: Configure ADC and GPIO for input.
  - `joystickGetLAnalogX/Y()`, `joystickGetRAnalogX/Y()`: Read analog axes.
  - `joystickGetLBtnUp/Down/Left/Right()`, etc.: Read button states.

### Buzzer (buzzer.c/h)
- Controls sound output for effects and music.
- Supports multiple tracks and callback-based playback.
- Internal functions:
  - `buzzerInit()`: Set up timers and output pins.
  - `buzzerPlay()`, `buzzerPause()`, `buzzerResume()`, `buzzerStop()`: Playback control.
  - `buzzerPlayWithFlag()`: Play sound and trigger callback on completion.

### Asset Loader (asset_loader.c/h)
- Loads game assets (tiles, sounds, etc.) from SD card or flash.
- Provides asset size, data, and header information to games.
- Internal functions:
  - `assetLoaderGetAssetMetadata()`: Query asset size by ID.
  - `assetLoaderGetAssetData()`: Load asset data into buffer.
  - `assetLoaderGetAssetHeader()`: Read asset metadata.

### SDIO & Filesystem (sdio.c/h, ff.c/h)
- Manages SD card interface and FAT filesystem.
- Internal functions:
  - `f_mount()`: Mount filesystem.
  - `sdioInit()`: Initialize SDIO hardware.
  - `ff_*()`: FATFS file operations.

### Interrupts & Syscalls
- **stm32f4xx_it.c**: All core and peripheral interrupt handlers.
- **syscalls.c**: Minimal C runtime support for embedded environment.

---

## Initialization Sequence
1. **peripheralsInit()**: Initializes SD card, DMA, GPIO, USART, Timer, LCD, ADC, Joystick, Buzzer, Renderer.
2. **gameConsoleExposeApi()**: Populates the API struct and exposes it to loaded games.

---

## Example Usage
Read joystick input:
```c
void update() {
    if (joystickGetLAnalogX() == JoystickAnalogValueHighAxis) { /* move right */ }
    if (joystickGetLAnalogX() == JoystickAnalogValueLowAxis)  { /* move left */ }
    if (joystickGetLAnalogY() == JoystickAnalogValueHighAxis) { /* move down */ }
    if (joystickGetLAnalogY() == JoystickAnalogValueLowAxis)  { /* move up */ }
}
```

---

## File Structure
- `Console/Src/`: Main source files
- `Console/Inc/`: Headers
- `GameXO/`: Example game
- `Shared/`, `Assets/` (`Music/`): Shared resources

---

## Subdirectory Structure and Module Overview

The `Console` directory is organized into several submodules, each with its own header (`.h`) and source (`.c`) files. Here is a breakdown of the main subdirectories and their responsibilities:

### Devices
- **Files:** `buzzer.[ch]`, `ILI9341.[ch]`, `joystick.[ch]`
- **Purpose:** Low-level drivers for hardware devices.
  - `buzzer`: Sound output and control.
  - `ILI9341`: LCD display driver.
  - `joystick`: Input from analog/digital joysticks.

### Renderer
- **Files:** `renderer.[ch]`
- **Purpose:** Scanline-based graphics engine for the ILI9341 320×240 display.

### Loader
- **Files:** `asset_loader.[ch]`, `game_loader.[ch]`, `loader.[ch]`
- **Purpose:** Asset and game loading from storage. Handles asset metadata, data transfer, and game selection.

### Peripherals
- **Files:** `adc.[ch]`, `dma.[ch]`, `gpio.[ch]`, `sdio.[ch]`, `sysclock.[ch]`, `timer.[ch]`, `usart.[ch]`
- **Purpose:** Abstraction for microcontroller peripherals. Used by higher-level modules for hardware access.

### FatFs
- **Files:** `diskio.[ch]`, `diskio_integration.[ch]`, `ff.[ch]`, `ffconf.h`, `ffunicode.c`
- **Purpose:** FAT filesystem support for SD card storage. Integrates with SDIO and provides file access to games and assets.

---

Each submodule is designed for separation of concerns, making the codebase modular and maintainable. For more details, see the respective header files in each subdirectory.

---

## See Also
- `game_console_api.h`: Full API definition
- `main.c`: Main application logic
- `game_console.c`: API exposure and initialization

---

## License
See `README.md` for license and authorship.

---

## Module Internals and Example Usages

### Devices
#### Buzzer
- **Purpose:** Sound output for effects and music.
- **Key Defines:** Musical note frequencies (e.g., `NOTE_C4`, `NOTE_D4`, ...).
- **Key Functions:**
  - `buzzerInit()`: Initialize hardware.
  - `buzzerPlay(track, data, size)`: Play a sequence of notes.
  - `buzzerPause(track)`, `buzzerResume(track)`, `buzzerStop(track)`: Playback control.
- **Usage Example:**
  ```c
  buzzerPlay(0, melody_data, melody_size);
  buzzerPause(0);
  buzzerResume(0);
  buzzerStop(0);
  ```

#### Joystick
- **Purpose:** Read analog and digital joystick input.
- **Key Types:**
  - `JoystickAnalogValue` enum: `Off`, `LowAxis`, `HighAxis`.
- **Key Functions:**
  - `joystickInit()`: Initialize ADC/GPIO.
  - `joystickGetLAnalogX/Y()`, `joystickGetRAnalogX/Y()`: Analog axes.
  - `joystickGetLBtnUp/Down/Left/Right()`, etc.: Button states.
- **Usage Example:**
  ```c
  if (joystickGetLBtnUp()) { /* move up */ }
  if (joystickGetRAnalogX() == JoystickAnalogValueHighAxis) { /* move right */ }
  ```

#### ILI9341 (LCD)
- **Purpose:** Low-level LCD display driver.
- **Key Defines:** Color constants (e.g., `ILI9341_BLACK`, `ILI9341_WHITE`), screen size.
- **Key Functions:**
  - `ili9341Init(rotation, width, height)`: Initialize display.
  - `ili9341DrawPixel(x, y, color)`: Draw a pixel.
  - `ili9341FillScreen(color)`: Fill areas.
  - `ili9341DrawImage(x, y, w, h, data)`: Draw image.
- **Usage Example:**
  ```c
  ili9341FillScreen(ILI9341_BLACK);
  ili9341DrawPixel(10, 10, ILI9341_RED);
  ```

### Renderer
- **Purpose:** Scanline-based graphics engine for the ILI9341 display.
- **Key Functions:**
  - `rendererInit()`: Initialize scanline buffers.
  - `rendererRender()`: Draw the current frame.
  - `rendererGetWidthPixels()`, `rendererGetHeightPixels()`: Screen dimensions (320×240).
- **Usage Example:**
  ```c
  rendererInit();
  rendererRender();
  ```

### Loader
#### Asset Loader
- **Purpose:** Load assets (tiles, sounds, etc.) from storage.
- **Key Functions:**
  - `assetLoaderGetAssetMetadata(asset_id, *size)`: Get asset size.
  - `assetLoaderGetAssetData(asset_id, *buffer)`: Load asset data.
  - `assetLoaderGetAssetHeader(*header)`: Get asset metadata.
- **Usage Example:**
  ```c
  uint32_t size;
  assetLoaderGetAssetMetadata(15, &size);
  uint8_t buffer[size];
  assetLoaderGetAssetData(15, buffer);
  ```

#### Game Loader
- **Purpose:** Load and manage game binaries.
- **Key Functions:**
  - `gameLoaderLoadGame(index)`: Load game by index.
  - `gameLoaderGetHeader(*header)`: Get game metadata.
  - `gameLoaderCloseGame()`: Unload game.
- **Usage Example:**
  ```c
  gameLoaderLoadGame(0);
  GameBinaryHeader header;
  gameLoaderGetHeader(&header);
  gameLoaderCloseGame();
  ```

#### Loader (File Loader)
- **Purpose:** File access and management for game binaries.
- **Key Functions:**
  - `loaderOpenFile(index)`: Open file by index.
  - `loaderGetFile()`: Get file handle.
  - `loaderCloseFile()`: Close file.
  - `loaderIsFileOpened()`: Check if file is open.
  - `loaderGetBinaryFilesNumberInDirectory()`: Count binaries.
  - `loaderGetFilenameByIndex(index, *out, *len)`: Get filename.
- **Usage Example:**
  ```c
  loaderOpenFile(0);
  FIL *file = loaderGetFile();
  loaderCloseFile();
  ```

# Renderer: Full Documentation

## Overview
The renderer is a scanline-based graphics engine targeting the ILI9341 320×240 display. It uses double-buffered scanline strips for DMA-friendly pixel transfer to the LCD.

## Graphics Model
- **Screen Size:** 320×240 pixels
- **System Palette:** 64 fixed RGB565 colors
- **Scanline Buffers:** Two buffers of 16 scanlines × 320 pixels each (`RENDERER_SCANLINE_BUFFERS` × `RENDERER_WIDTH`), enabling double-buffered rendering

## API
The renderer exposes a minimal public API:
- `rendererInit()` — initialize scanline buffers
- `rendererRender()` — render the current frame to the ILI9341
- `rendererGetWidthPixels()` / `rendererGetHeightPixels()` — query screen dimensions

## Usage
```c
rendererInit();
// In your game loop:
rendererRender();
```

## System Palette
![system_palette](docu/system_palette.png)

## Reference
- See `renderer.h` and `renderer.c` for implementation details.

