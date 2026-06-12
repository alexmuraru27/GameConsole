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

Use `api_hdr_ptr->` to call any API function, e.g. `api_hdr_ptr->renderer->...`.

### 4. Define the game binary header

At file scope (typically end of main file):

```c
DECLARE_GAME_BINARY_HEADER(game_header, main);
```

Required for the loader to recognize the binary layout and load the game.

### 5. Define assets

Start with the asset header (required for asset discovery):

```c
DEFINE_ASSET_HEADER(ASSET_MAGIC, ASSET_VERSION, ASSET_COUNT);
```

Then define each asset:

```c
DEFINE_ASSET_8(font_a,         ASSET_ID_FONT_A,          ASSET_TYPE_FONT,           { /* data */ });
DEFINE_ASSET_16(audio_data,    ASSET_ID_AUDIO_DATA,       ASSET_TYPE_AUDIO_DATA,     { /* data */ });
DEFINE_ASSET_16(audio_duration,ASSET_ID_AUDIO_DURATION,   ASSET_TYPE_AUDIO_DURATION, { /* data */ });
```

Assets are lazy-loaded at runtime via `assetLoaderGetAssetData()`, so total asset data can exceed what fits in RAM at once.

For tile data from the [Tile Creator](../tools/graphics/README.md), use the generated define directly:

```c
DEFINE_ASSET_8(my_tile, ASSET_ID_MY_TILE, ASSET_TYPE_TILE, (DEFINE_MY_TILE_DATA));
```

### 6. Build and deploy

Build with the provided Makefile and copy the resulting `.bin` to the SD card root directory.

## Minimal main.c example

```c
#include "game_console_api.h"
#include "assets.h"

DECLARE_API_HEADER_PTR(api_hdr_ptr);

int main(void)
{
    if (api_hdr_ptr->magic == API_MAGIC && api_hdr_ptr->version == API_VERSION)
    {
        printf("Hello from my game!\r\n");
    }
    return 0;
}

DECLARE_GAME_BINARY_HEADER(game_header, main);
```

## Reference

- [ConsoleAPI reference](API_README.md)
- [Memory layout](../CLAUDE.md)
- [Example game source](../GameXO/)
