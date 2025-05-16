#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>
#include "stdbool.h"

void rendererInit(void);
void rendererRender(void);
void rendererTriggerCompleteRedraw(void);

// Renderer sizes
uint16_t rendererGetSizeWidth();
uint16_t rendererGetSizeHeight();
uint16_t rendererGetSizeTileScreen();
uint16_t rendererGetSizeTileMemory();
uint16_t rendererGetSizeFramePalette();
uint16_t rendererGetSizePatternTable();
uint16_t rendererGetSizeNameTable();
uint16_t rendererGetSizeOam();
uint16_t rendererGetMaxTilesInRow();
uint16_t rendererGetMaxTilesInColumn();

// Frame Palette
void rendererFramePaletteSetSprite(uint8_t palette_index, uint8_t color_index, uint8_t system_palette_index);
void rendererFramePaletteSetSpriteMultiple(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);
void rendererFramePaletteSetBackground(uint8_t palette_index, uint8_t color_index, uint8_t system_palette_index);
void rendererFramePaletteSetBackgroundMultiple(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);

// Pattern table
void rendererPatternTableSetTile(uint8_t table_index, const uint8_t *tile_data, uint8_t tile_size);
void rendererPatternTableClear(uint8_t system_color);

// Name table
void rendererNameTableSetTile(uint8_t x, uint8_t y, uint8_t tile_idx);
void rendererNameTableClearTile(uint8_t table_index);

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

// AttributeTable
void rendererAttributeTableSetPalette(uint8_t nametable_idx, uint8_t palette);
uint8_t rendererAttributeTableGetPalette(uint8_t nametable_idx);

#endif /* __RENDERER_H */