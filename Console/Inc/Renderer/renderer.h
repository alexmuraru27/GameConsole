#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>
#include "stdbool.h"

#define RENDERER_WIDTH 256U  // 32
#define RENDERER_HEIGHT 240U // 30
#define RENDERER_TILE_SCREEN_SIZE 16U
#define RENDERER_TILE_MEMORY_SIZE 64U

void rendererInit(void);
void rendererRender(void);
void rendererTriggerCompleteRedraw(void);

// Frame Palette
void rendererPaletteSetSprite(uint8_t palette_index, uint8_t color_index, uint8_t system_palette_index);
void rendererPaletteSetSpriteMultiple(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);
void rendererPaletteSetBackground(uint8_t palette_index, uint8_t color_index, uint8_t system_palette_index);
void rendererPaletteSetBackgroundMultiple(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);

// Pattern table
void rendererPatternTableSetTile(uint8_t table_index, const uint8_t *tile_data, uint8_t tile_size);
void rendererPatternTableClear(uint8_t system_color);

// Name table
void rendererNameTableSetTile(uint8_t table_index, uint8_t tile_idx);
void rendererNameTableClear();

// Oam
void rendererOamClearEntry(uint8_t oam_idx);

// Oam setters
void rendererOamSetXPos(uint8_t oam_idx, uint8_t x_pos);
void rendererOamSetFlipV(uint8_t oam_idx, bool is_flip_v);
void rendererOamSetFlipH(uint8_t oam_idx, bool is_flip_h);
void rendererOamSetPriority(uint8_t oam_idx, bool is_priority);
void rendererOamSetIsDirty(uint8_t oam_idx, bool is_dirty);
void rendererOamSetPaletteIdx(uint8_t oam_idx, uint8_t palette_idx);
void rendererOamSetTileIdx(uint8_t oam_idx, uint8_t tile_idx);
void rendererOamSetYPos(uint8_t oam_idx, uint8_t y_pos);

// Oam getters
uint8_t rendererOamGetXPos(uint8_t oam_idx);
bool rendererOamGetFlipV(uint8_t oam_idx);
bool rendererOamGetFlipH(uint8_t oam_idx);
bool rendererOamGetPriority(uint8_t oam_idx);
bool rendererOamGetIsDirty(uint8_t oam_idx);
uint8_t rendererOamGetPaletteIdx(uint8_t oam_idx);
uint8_t rendererOamGetTileIdx(uint8_t oam_idx);
uint8_t rendererOamGetYPos(uint8_t oam_idx);

#endif /* __RENDERER_H */