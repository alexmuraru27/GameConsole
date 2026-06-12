# Tile Creator

A pixel-art tile editor that outputs C headers for the GameConsole renderer.

**Script:** `tile_creator.py`  
**Requires:** `pygame`

## Usage

```bash
python3 tools/graphics/tile_creator.py
```

Output is written to `generated_tiles/`.

![Tile Creator UI](../../docu/tile_creator_docu.png)

## Controls

| Input       | Action                         |
| ----------- | ------------------------------ |
| Left click  | Draw                           |
| Right click | Erase                          |
| RCtrl + S   | Save (by textbox name)         |
| RCtrl + L   | Load (by textbox name)         |

Click one of the 4 large palette squares to select the active color (slot 0 is always transparent).  
With a slot selected, pick a color from the system palette grid at the top to reassign it.  
The full system palette (64 RGB565 colors) is shown in `docu/system_palette.png`.

## Output Format

Each save produces a C header in `generated_tiles/`:

```c
#ifndef __bricks1_H
#define __bricks1_H
#include "tileCreator.h"
const uint8_t bricks1_data[64U] = DEFINE_TILE(
    2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2,
    ...);
const uint8_t bricks1_palette[4U] = {0x30, 0x7, 0x17, 0x27};
#endif
```

Tile data plugs directly into `DEFINE_ASSET_8` in game source:

```c
DEFINE_ASSET_8(my_tile, ASSET_ID_MY_TILE, ASSET_TYPE_TILE, (DEFINE_MY_TILE_DATA));
```
