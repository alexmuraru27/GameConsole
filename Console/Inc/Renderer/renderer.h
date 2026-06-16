#ifndef __RENDERER_H
#define __RENDERER_H

#include <stdint.h>
#include "stdbool.h"

#define RENDERER_WIDTH 320U
#define RENDERER_HEIGHT 240U

typedef enum
{
    /* Bits 0-1 mirror the Pixel Forge asset (gfx_asset.h): pixel format + opacity. */
    SPRITE_IS_FMT_4BPP = (1U << 0), /* pixel format: 0 = 2bpp, 1 = 4bpp */
    SPRITE_OPAQUE = (1U << 1),      /* no transparent pixels: skip the per-pixel index==0 test */
    /* Bits 2+ are set by game logic. */
    SPRITE_FLIP_H = (1U << 2),
    SPRITE_FLIP_V = (1U << 3),
} SpriteFlags;

typedef enum
{
    LAYER_BG = 0,
    LAYER_FG = 1,
    LAYER_UI = 2,
    LAYER_COUNT = 3
} Layer;

typedef struct
{
    int16_t x, y;
    uint16_t w, h;
    uint8_t z;
    uint8_t flags; /* SpriteFlags (format lives in bit 0, opacity in bit 1) */
    uint16_t id;   /* free: fills the byte reclaimed by folding format into flags */
    const uint8_t *pixels;
    const uint16_t *palette;
} Sprite;

void rendererInit(void);
void rendererClear(void);
void rendererSetBackground(uint16_t color); /* RGB565 painted where no sprite draws */
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count);
void rendererRender(void);

uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);

#endif /* __RENDERER_H */
