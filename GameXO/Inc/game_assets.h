#ifndef __GAME_ASSETS_H
#define __GAME_ASSETS_H

#include "game_console_api.h"
#include <stdbool.h>

/*
 * Thin helpers over the ConsoleAPI for loading this game's packed assets and
 * drawing text. Graphics are streamed from GameXO.pak into the CCM asset arena
 * (GAME_RAM_ASSET); sound tracks are streamed and handed to the buzzer; text is
 * drawn with the console's built-in fonts (no glyphs are packed in the game).
 */

/* Reset the CCM asset arena. Call once before loading any graphics. */
void gameAssetsInit(void);

/* Load a packed GfxAsset by id into the CCM arena and fill `sprite_out` to point
 * at it (x/y/z = 0; caller positions it). The RGB565 palette is written into
 * `palette_out` (must hold >= 16 entries) and must outlive the sprite. */
bool gameAssetsLoadSprite(uint32_t asset_id, Sprite *sprite_out, uint16_t *palette_out);

/* Load a packed music track by id and play it on buzzer track 0 (stopping any
 * current playback). The notes are kept in a persistent buffer for the buzzer. */
bool gameAssetsPlaySound(uint32_t asset_id);

/* Pixel width of `text` in the given console font (for centering). */
uint16_t gameAssetsTextWidth(FontSize size, const char *text);

/* Emit one sprite per glyph of `text` into `out` (capacity `max`); returns the
 * number written. Glyph pixels come from console flash, so they stay valid. */
uint16_t gameAssetsDrawText(Sprite *out, uint16_t max, FontSize size,
                            int16_t x, int16_t y, uint8_t z,
                            const uint16_t *palette, const char *text);

/* Like gameAssetsDrawText but nearest-neighbour scaled by `factor`; the scaled
 * glyph pixels are baked into `pool` (size `pool_size`), which must stay alive
 * until the frame is rendered. */
uint16_t gameAssetsDrawTextScaled(Sprite *out, uint16_t max, FontSize size,
                                  int16_t x, int16_t y, uint8_t z, uint8_t factor,
                                  const uint16_t *palette, const char *text,
                                  uint8_t *pool, uint16_t pool_size);

#endif /* __GAME_ASSETS_H */
