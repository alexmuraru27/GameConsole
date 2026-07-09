#ifndef __RENDERER_INTERNAL_H
#define __RENDERER_INTERNAL_H

#include <stdint.h>
#include "renderer_interface.h" /* Sprite */

/*
 * Private seam between the renderer core (renderer.c — the -O3 per-frame compositor
 * + z-sort) and the cold console-text path (renderer_text.c — rendererDrawText, the
 * scaled-glyph cache, the tint-palette pool). A frame's text glyphs live in the
 * arrays below, tagged by layer; the z-sort folds them into each layer's run.
 *
 * The state stays DEFINED in renderer.c so the hot z-sort and rendererRender read it
 * same-TU (their machine code is unchanged by the split); renderer_text.c reaches it
 * through these declarations. Not part of the public renderer.h.
 */
#define TEXT_SPRITE_CAP 256U
#define TEXT_PALETTE_CAP 16U

extern Sprite s_text_sprites[TEXT_SPRITE_CAP]; /* one glyph each, this frame */
extern uint8_t s_text_layer[TEXT_SPRITE_CAP];  /* the layer each glyph belongs to */
extern uint16_t s_text_count;                  /* glyphs queued this frame */
extern uint16_t s_text_palettes[TEXT_PALETTE_CAP][4]; /* pooled {0,ink,ink,ink} tints */
extern uint16_t s_text_palette_count;
extern uint32_t s_frame_counter; /* bumped once per rendererRender; glyph-cache LRU key */

#endif /* __RENDERER_INTERNAL_H */
