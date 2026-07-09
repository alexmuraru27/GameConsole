// Font utility implementations.
#include "Fonts/font_utils.h"
#include "Fonts/fonts.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Per-size dimensions and font pointers.                            */
/* ------------------------------------------------------------------ */
static const uint8_t s_fontW[] = {
    [FONT_3x5] = 3U,
    [FONT_5x5] = 5U,
    [FONT_8x8] = 8U,
};
static const uint8_t s_fontH[] = {
    [FONT_3x5] = 5U,
    [FONT_5x5] = 5U,
    [FONT_8x8] = 8U,
};
static const Font *s_fontBySize[] = {
    [FONT_3x5] = &font3x5,
    [FONT_5x5] = &font5x5,
    [FONT_8x8] = &font8x8,
};

uint16_t fontGlyphW(FontSize size)
{
    return s_fontW[size];
}

uint16_t fontGlyphH(FontSize size)
{
    return s_fontH[size];
}

void fontGet(uint8_t ch, FontSize size, const uint8_t **pixels)
{
    const Font *font = s_fontBySize[size];
    *pixels = font->glyphs[ch - 0x20U].pixels;
}

/* ------------------------------------------------------------------ */
/*  Scaled-glyph size and the scaler itself.                          */
/* ------------------------------------------------------------------ */

/* Glyph bitmaps are 2 bits/pixel, packed MSB-first, 4 pixels per byte: column 0
 * lives in bits 7-6, column 3 in bits 1-0. Slot 0 is transparent. */
#define FONT_BPP             2U
#define FONT_PIXELS_PER_BYTE 4U /* 8 / FONT_BPP */
#define FONT_SLOT_MASK       3U /* (1 << FONT_BPP) - 1 */

/* Bytes needed for one row of `w` pixels at FONT_BPP. */
static inline uint8_t fontRowStride(uint8_t w)
{
    return (uint8_t)((w * FONT_BPP + 7U) / 8U);
}

/* Byte index / bit shift of the 2-bit slot for pixel column `x`. */
static inline uint8_t fontSlotByte(uint8_t x)
{
    return (uint8_t)(x / FONT_PIXELS_PER_BYTE);
}
static inline uint8_t fontSlotShift(uint8_t x)
{
    return (uint8_t)(6U - (x % FONT_PIXELS_PER_BYTE) * FONT_BPP);
}

/* Dimensions of a glyph scaled by `scale`: pixel width/height and the packed
 * row stride in bytes. The single source of the scaled-glyph geometry. */
static void fontScaledDims(FontSize size, uint8_t scale,
                           uint8_t *outW, uint8_t *outH, uint8_t *outStride)
{
    *outW      = (uint8_t)(s_fontW[size] * scale);
    *outH      = (uint8_t)(s_fontH[size] * scale);
    *outStride = fontRowStride(*outW);
}

uint16_t fontSize(FontSize size, uint8_t scale)
{
    uint8_t outW, outH, outStride;
    fontScaledDims(size, scale, &outW, &outH, &outStride);
    (void)outW;
    return (uint16_t)outStride * outH;
}

void fontScale(uint8_t ch, FontSize size, uint8_t scale, uint8_t *dst)
{
    const uint8_t *src;
    fontGet(ch, size, &src);

    uint8_t inW      = s_fontW[size];
    uint8_t inH      = s_fontH[size];
    uint8_t inStride = fontRowStride(inW);
    uint8_t outW, outH, outStride;
    fontScaledDims(size, scale, &outW, &outH, &outStride);
    (void)outW;

    memset(dst, 0, (uint16_t)outStride * outH);

    for (uint8_t srcY = 0U; srcY < inH; srcY++)
    {
        for (uint8_t srcX = 0U; srcX < inW; srcX++)
        {
            uint8_t srcByte = src[srcY * inStride + fontSlotByte(srcX)];
            uint8_t slot    = (srcByte >> fontSlotShift(srcX)) & FONT_SLOT_MASK;

            if (slot == 0U)
            {
                continue;
            }

            uint8_t blockX = (uint8_t)(srcX * scale);
            uint8_t blockY = (uint8_t)(srcY * scale);
            for (uint8_t dstY = 0U; dstY < scale; dstY++)
            {
                uint8_t dstRow = (uint8_t)(blockY + dstY);
                for (uint8_t dstX = 0U; dstX < scale; dstX++)
                {
                    uint8_t dstCol   = (uint8_t)(blockX + dstX);
                    uint8_t *dstByte = &dst[dstRow * outStride + fontSlotByte(dstCol)];
                    *dstByte         = (uint8_t)(*dstByte | (slot << fontSlotShift(dstCol)));
                }
            }
        }
    }
}
