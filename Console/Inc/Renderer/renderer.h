#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>

void rendererInit(void);
void rendererRender(void);
void rendererTriggerCompleteRedraw(void);

// Palette
void rendererPaletteSetSprite(uint8_t pallete_index, uint8_t color_index, uint8_t system_pallete_index);
void rendererPaletteSetBackground(uint8_t pallete_index, uint8_t color_index, uint8_t system_pallete_index);

// Pattern table
void rendererPatternTableSetTile(uint8_t table_index, const uint8_t *tile_data, uint8_t tile_size);
void rendererPatternTableClear(uint8_t system_color);

#endif /* __RENDERER_H */