// Font utilities: metrics, pixel lookup, scaling.
#ifndef __FONT_UTILS_H
#define __FONT_UTILS_H

#include <stdint.h>
#include "font_types.h"

uint16_t fontGlyphW(FontSize size);
uint16_t fontGlyphH(FontSize size);

/* Unscaled glyph pixels for a printable-ASCII character (0x20-0x7E). */
void fontGet(uint8_t ch, FontSize size, const uint8_t **pixels);

/* Scaled-glyph output buffer size, in bytes. */
uint16_t fontSize(FontSize size, uint8_t scale);

/* Scale a printable-ASCII glyph into dst.  Nearest-neighbour integer
 * scaling; scale 1 is a copy.  Src and dst must not overlap. */
void scaleFont(uint8_t ch, FontSize size, uint8_t scale, uint8_t *dst);

#endif /* __FONT_UTILS_H */
