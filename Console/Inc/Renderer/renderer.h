#ifndef __RENDERER_H
#define __RENDERER_H
#include <stdint.h>
#include "stdbool.h"

void rendererInit(void);

// Trigger rendering
void rendererRender(void);

// Renderer sizes
uint16_t rendererGetWidthPixels();
uint16_t rendererGetHeightPixels();

#endif /* __RENDERER_H */