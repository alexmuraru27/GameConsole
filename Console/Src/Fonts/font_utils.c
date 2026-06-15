// Font utility implementations.
#include "font_utils.h"
#include "fonts.h"
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

uint16_t fontSize(FontSize size, uint8_t scale)
{
    uint8_t inW       = s_fontW[size];
    uint8_t inH       = s_fontH[size];
    uint8_t outW      = (uint8_t)(inW * scale);
    uint8_t outH      = (uint8_t)(inH * scale);
    uint8_t outStride = (uint8_t)((outW * 2U + 7U) / 8U);
    return (uint16_t)outStride * outH;
}

void scaleFont(uint8_t ch, FontSize size, uint8_t scale, uint8_t *dst)
{
    const uint8_t *src;
    fontGet(ch, size, &src);

    uint8_t inW       = s_fontW[size];
    uint8_t inH       = s_fontH[size];
    uint8_t outW      = (uint8_t)(inW * scale);
    uint8_t outH      = (uint8_t)(inH * scale);
    uint8_t outStride = (uint8_t)((outW * 2U + 7U) / 8U);
    uint8_t inStride  = (uint8_t)((inW * 2U + 7U) / 8U);

    memset(dst, 0, (uint16_t)outStride * outH);

    for (uint8_t srcY = 0U; srcY < inH; srcY++)
    {
        for (uint8_t srcX = 0U; srcX < inW; srcX++)
        {
            uint8_t srcByte  = src[srcY * inStride + (srcX >> 2U)];
            uint8_t srcShift = (uint8_t)(6U - (srcX & 3U) * 2U);
            uint8_t slot     = (srcByte >> srcShift) & 3U;

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
                    uint8_t *dstByte = &dst[dstRow * outStride + (dstCol >> 2U)];
                    uint8_t dstShift = (uint8_t)(6U - (dstCol & 3U) * 2U);
                    *dstByte = (uint8_t)(*dstByte | (slot << dstShift));
                }
            }
        }
    }
}
