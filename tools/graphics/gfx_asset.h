#ifndef GFX_ASSET_H
#define GFX_ASSET_H

#include <stdint.h>

#define GFX_ASSET_MAGIC 0x31584647U /* "GFX1" little-endian */

#define GFX_PALETTE_COLORS_2BPP 4U
#define GFX_PALETTE_COLORS_4BPP 16U

// GfxAssetHeader.flags bits
#define GFX_FLAG_OPAQUE 0x1U // picture has no transparent (slot 0) pixels

typedef enum
{
    GFX_FMT_2BPP = 1,
    GFX_FMT_4BPP = 2,
} GfxFormat;

typedef struct __attribute__((packed))
{
    uint32_t magic;    // "GFX1"
    uint32_t width;    // pixels
    uint32_t height;   // pixels
    uint32_t format;   // GfxFormat
    uint32_t dataSize; // pixel data bytes after this header
    uint32_t flags;    // GFX_FLAG_* bitmask
} GfxAssetHeader;

// Maps the start of a .bin file:
// [GfxAssetHeader]
// [pixel rows] dataSize bytes, row-major, each row padded to a byte boundary;
//              2bpp = 4 px/byte MSB-first, 4bpp = 2 px/byte high nibble first
//
// The pixel data is followed by the palette: one byte per color (a system
// palette index 0-63), GFX_PALETTE_COLORS_2BPP/_4BPP entries depending on
// format. Entry 0 is the transparent/background slot; its stored color is the
// picture's suggested backdrop color. GFX_FLAG_OPAQUE is set when the picture
// uses no slot-0 pixels, so a consumer can render it on the fast opaque path.
typedef struct __attribute__((packed))
{
    GfxAssetHeader header;
    uint8_t data[]; // header.dataSize pixel bytes
} GfxAsset;

#endif
