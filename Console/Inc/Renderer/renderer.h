#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>
#include "stdbool.h"

void rendererInit(void);

// Trigger rendering
void rendererRender(void);

// Complete redraw
void rendererSetDirtyCompleteRedraw(void);

// Renderer sizes
uint16_t rendererGetWidthPixels();
uint16_t rendererGetHeightPixels();
uint16_t rendererGetWidthTiles();
uint16_t rendererGetHeightTiles();
uint16_t rendererGetTilePixelSize();
uint16_t rendererGetTileMemorySize();
uint16_t rendererGetFramePaletteSize();
uint16_t rendererGetFrameSubPaletteSize();
uint16_t rendererGetPatternTableSize();
uint16_t rendererGetNameTableSize();
uint16_t rendererGetOamSize();

// Frame Palette
void rendererFramePaletteSetSprite(uint8_t palette_idx, uint8_t color_idx, uint8_t system_palette_idx);
void rendererFramePaletteSetSpriteMultiple(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);
void rendererFramePaletteSetBackground(uint8_t palette_idx, uint8_t color_idx, uint8_t system_palette_idx);
void rendererFramePaletteSetBackgroundMultiple(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);

// Pattern table
void rendererPatternTableSetTile(uint8_t pattern_table_idx, const uint8_t *tile_data, uint8_t tile_size);
void rendererPatternTableClear();

// Name table
void rendererNameTableSetTile(uint8_t tile_x, uint8_t tile_y, uint8_t pattern_table_idx);

// Oam
void rendererOamClearEntry(uint8_t oam_idx);

// Oam setters
void rendererOamSetXYPos(uint8_t oam_idx, uint8_t x_pos, uint8_t y_pos);
void rendererOamSetFlipV(uint8_t oam_idx, bool is_flip_v);
void rendererOamSetFlipH(uint8_t oam_idx, bool is_flip_h);
void rendererOamSetPriorityLow(uint8_t oam_idx, bool is_priority_low);
void rendererOamSetPaletteIdx(uint8_t oam_idx, uint8_t palette_idx);
void rendererOamSetTileIdx(uint8_t oam_idx, uint8_t tile_idx);

// Oam getters
uint8_t rendererOamGetXPos(uint8_t oam_idx);
bool rendererOamGetFlipV(uint8_t oam_idx);
bool rendererOamGetFlipH(uint8_t oam_idx);
bool rendererOamGetPriorityLow(uint8_t oam_idx);
uint8_t rendererOamGetPaletteIdx(uint8_t oam_idx);
uint8_t rendererOamGetTileIdx(uint8_t oam_idx);
uint8_t rendererOamGetYPos(uint8_t oam_idx);

// AttributeTable XY Coords addressing
void rendererAttributeTableSetPalette(uint8_t tile_x, uint8_t tile_y, uint8_t palette);
uint8_t rendererAttributeTableGetPalette(uint8_t tile_x, uint8_t tile_y);
void rendererAttributeTableSetFlipV(uint8_t tile_x, uint8_t tile_y, bool isFlipV);
bool rendererAttributeTableGetFlipV(uint8_t tile_x, uint8_t tile_y);
void rendererAttributeTableSetFlipH(uint8_t tile_x, uint8_t tile_y, bool isFlipH);
bool rendererAttributeTableGetFlipH(uint8_t tile_x, uint8_t tile_y);
void rendererAttributeTableSetPriorityHigh(uint8_t tile_x, uint8_t tile_y, bool is_priority_high);
bool rendererAttributeTableGetPriorityHigh(uint8_t tile_x, uint8_t tile_y);

#endif /* __RENDERER_H */