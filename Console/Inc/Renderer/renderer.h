#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>

void rendererInit(void);
void rendererRender(void);
void rendererSetPaletteSprite(uint8_t pallete_index, uint8_t color_index, uint8_t system_pallete_index);
void rendererSetPaletteBackground(uint8_t pallete_index, uint8_t color_index, uint8_t system_pallete_index);
void rendererTriggerCompleteRedraw(void);
#endif /* __RENDERER_H */