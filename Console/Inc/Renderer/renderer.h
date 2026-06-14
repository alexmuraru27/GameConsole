#ifndef __RENDERER_H
#define __RENDERER_H

#include <stdint.h>
#include "stdbool.h"

#define RENDERER_WIDTH 320U
#define RENDERER_HEIGHT 240U

typedef enum
{
    SPRITE_FLIP_H = (1U << 0),
    SPRITE_FLIP_V = (1U << 1),
    SPRITE_OPAQUE = (1U << 2), /* no transparent pixels: skip the per-pixel index==0 test */
} SpriteFlags;

typedef enum
{
    GFX_FMT_2BPP = 1,
    GFX_FMT_4BPP = 2,
} GfxFormat;

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
    uint8_t flags;
    uint8_t format;
    const uint8_t *pixels;
    const uint16_t *palette;
} Sprite;

void rendererInit(void);
void rendererClear(void);
void rendererSubmit(Layer layer, const Sprite *sprite);
void rendererRender(void);

uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);

#endif /* __RENDERER_H */
