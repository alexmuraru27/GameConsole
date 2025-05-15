#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>
#include "stdbool.h"
void rendererInit(void);
void rendererRender(void);
void rendererTriggerCompleteRedraw(void);

// Palette
void rendererPaletteSetSprite(uint8_t pallete_index, uint8_t color_index, uint8_t system_pallete_index);
void rendererPaletteSetBackground(uint8_t pallete_index, uint8_t color_index, uint8_t system_pallete_index);

// Pattern table
void rendererPatternTableSetTile(uint8_t table_index, const uint8_t *tile_data, uint8_t tile_size);
void rendererPatternTableClear(uint8_t system_color);

// Name table
void rendererNameTableSetTile(uint8_t table_index, uint8_t tile_idx);
void rendererNameTableClear();

// Oam
void rendererOamClearIndex(uint8_t oam_idx);

// Oam setters
void rendererOamSetXPos(uint8_t oam_idx, uint8_t x_pos);
void rendererOamSetFlipV(uint8_t oam_idx, bool is_flip_v);
void rendererOamSetFlipH(uint8_t oam_idx, bool is_flip_h);
void rendererOamSetPriority(uint8_t oam_idx, bool is_priority);
void rendererOamSetIsDirty(uint8_t oam_idx, bool is_dirty);
void rendererOamSetPalleteIdx(uint8_t oam_idx, uint8_t pallete_idx);
void rendererOamSetTileIdx(uint8_t oam_idx, uint8_t tile_idx);
void rendererOamSetYPos(uint8_t oam_idx, uint8_t y_pos);

// Oam getters
uint8_t rendererOamGetXPos(uint8_t oam_idx);
bool rendererOamGetFlipV(uint8_t oam_idx);
bool rendererOamGetFlipH(uint8_t oam_idx);
bool rendererOamGetPriority(uint8_t oam_idx);
bool rendererOamGetIsDirty(uint8_t oam_idx);
uint8_t rendererOamGetPalleteIdx(uint8_t oam_idx);
uint8_t rendererOamGetTileIdx(uint8_t oam_idx);
uint8_t rendererOamGetYPos(uint8_t oam_idx);

#endif /* __RENDERER_H */