#ifndef __bricks4_H
#define __bricks4_H
#include "tileCreator.h"
const uint8_t bricks4_data[64U] = DEFINE_TILE_16(
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 1, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3, 3, 3, 1, 1, 
	1, 3, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1, 
	1, 3, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1, 1, 
	1, 3, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 1, 1, 
	1, 2, 2, 2, 2, 1, 2, 2, 1, 2, 2, 2, 2, 1, 2, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
	1, 3, 3, 2, 1, 3, 3, 3, 3, 2, 2, 1, 3, 3, 2, 1, 
	1, 3, 2, 2, 1, 3, 2, 2, 2, 2, 2, 1, 3, 2, 2, 1, 
	1, 3, 2, 2, 1, 3, 2, 2, 2, 2, 2, 1, 3, 2, 2, 1, 
	1, 3, 2, 2, 1, 3, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 
	1, 2, 2, 2, 1, 3, 2, 2, 1, 2, 2, 1, 2, 2, 2, 1, 
	1, 2, 1, 2, 1, 2, 2, 1, 1, 2, 2, 1, 2, 2, 2, 1, 
	1, 2, 1, 2, 1, 2, 2, 1, 2, 2, 2, 1, 1, 2, 2, 1, 
	1, 1, 1, 2, 1, 2, 2, 2, 2, 2, 2, 1, 1, 2, 2, 1, 
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
const uint8_t bricks4_palette[4U] = {0x30, 0x7, 0x17, 0x27};
#endif