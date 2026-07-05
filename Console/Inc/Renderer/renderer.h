#ifndef __RENDERER_H
#define __RENDERER_H

#include <stdint.h>
#include "stdbool.h"
#include "renderer_interface.h" /* RENDERER_WIDTH/HEIGHT, SpriteFlags, Layer, Sprite */

void rendererInit(void);       /* boot-time: build compositor tables + clear buffers */
void rendererResetState(void); /* per-game launch: drop layers + disable background */
void rendererClear(void);
void rendererSetBackground(uint16_t color); /* RGB565 painted where no sprite draws */
/* Console-only: snapshot/restore the background-fill state so a full-screen OS
 * modal (the osTextInput keyboard) can borrow the screen over a running game and
 * hand it back untouched. Not game syscalls. */
void rendererGetBackground(bool *enabled, uint16_t *color);
void rendererSetBackgroundState(bool enabled, uint16_t color);
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count);
void rendererRender(void);

uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);

/* Map a Pixel Forge system-palette index (0-63) to RGB565. Games use this to
 * turn a loaded GfxAsset's index palette into a render-ready RGB565 palette. */
uint16_t rendererSystemColor(uint8_t system_index);

#endif /* __RENDERER_H */
