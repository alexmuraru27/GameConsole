# Game Creation Guide

How to create a new game for the GameConsole platform.

## Steps

### 1. Use the provided linker script

In your game's Makefile, set the linker script to `../game.ld`. This places the game binary in the correct memory regions for the loader.

### 2. Include the Console API header

```c
#include "game_console_api.h"
```

Gives access to all Console API functions and asset macros.

### 3. Declare the API header pointer (global scope)

```c
DECLARE_API_HEADER_PTR(api_hdr_ptr);
```

Call any API function through the `api` member, e.g. `api_hdr_ptr->api.rendererRender()` or `api_hdr_ptr->api.log("hi %d", n)`. Verify `api_hdr_ptr->magic == API_MAGIC` (and `version`) before the first call.

### 4. Define the game binary header

At file scope (typically the end of the main file), pass your **entry function** — conventionally `_game_start` from the game's `startup.s`, which runs `__libc_init_array` and then calls `main()`:

```c
extern void _game_start(void);
DECLARE_GAME_BINARY_HEADER(_game_start);
```

The macro takes a single argument and emits a `GameBinaryHeader` into the `.game_header` section (magic `"GAME"`, section boundaries, entry point) so the loader knows how to place each region and where to jump.

### 5. Add assets

Author assets with the desktop tools and bundle them into a `.pak`:

- [Pixel Forge](../tools/graphics/README.md) exports `GfxAsset` `.bin`/`.c` graphics (2bpp/4bpp, console palette).
- [Music Creator](../tools/music_creator/README.md) exports buzzer tracks as interleaved `(freq, ms)` `.bin`/`.c`.
- The [Asset Packer](../tools/packer/README.md) bundles the loose `.bin` files from a YAML manifest into one `<name>.pak` container plus a generated `<name>AssetEnum.h` of asset IDs.

At runtime, stream an asset by ID into a buffer carved from the CCM asset arena:

```c
AssetMetaData meta;
api_hdr_ptr->api.assetLoaderGetAssetMetadata(ASSET_ID_HERO, &meta);
uint8_t buffer[meta.asset_size];
api_hdr_ptr->api.assetLoaderGetAssetData(ASSET_ID_HERO, buffer, sizeof(buffer));
```

> **Not implemented yet:** `asset_loader.c` is a stub — `assetLoaderGetAssetData()` returns `ASSET_NOT_FOUND`. The `.pak` format and packer exist, but the on-device read from `.pak` is still a TODO, so packed assets cannot be loaded at runtime yet.

### 6. Build and deploy

Build with the provided Makefile and copy the resulting `.bin` (and, once the asset loader lands, the matching `.pak`) to the SD card root. `make deploy` copies the built `.bin` to the SD mount point configured in `common.mk`.

## Minimal main.c example

```c
#include "game_console_api.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);

int main(void)
{
    if (api_hdr_ptr->magic == API_MAGIC && api_hdr_ptr->version == 1U)
    {
        // Game-local printf() has no backing _write — log through the API instead.
        api_hdr_ptr->api.log("Hello from my game!");

        while (true)
        {
            // update + render here
            if (api_hdr_ptr->api.joystickGetSpecialBtn2())
            {
                break; // Special Button 2 returns to the console OS
            }
        }
    }
    return 0;
}

extern void _game_start(void);            // provided by the game's startup.s
DECLARE_GAME_BINARY_HEADER(_game_start);
```

## Reference

- [ConsoleAPI reference](API_README.md)
- [Memory layout](memory.md)
- [Example game source](../GameXO/)
