#ifndef __RENDERER_H
#define __RENDERER_H

#include <stdint.h>
#include "stdbool.h"
#include "renderer_interface.h" /* RENDERER_WIDTH/HEIGHT, SpriteFlags, Layer, Sprite */

void rendererInit(void);
void rendererClear(void);
void rendererSetBackground(uint16_t color); /* RGB565 painted where no sprite draws */
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count);
void rendererRender(void);

uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);

/* Map a Pixel Forge system-palette index (0-63) to RGB565. Games use this to
 * turn a loaded GfxAsset's index palette into a render-ready RGB565 palette. */
uint16_t rendererSystemColor(uint8_t system_index);

#endif /* __RENDERER_H */
