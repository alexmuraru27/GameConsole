#include "syscall_validate.h"

/* Region bounds come from the linker script (common.ld) as absolute symbols, so
 * the trust boundary tracks the real memory map instead of hard-coded addresses.
 * These are linker symbols — their *address* is the value, hence the &__sym casts. */
extern uint32_t __game_ram_start, __game_ram_size;
extern uint32_t __game_ram_asset_start, __game_ram_asset_size;
extern uint32_t __console_flash_start, __console_flash_size;

static bool rangeWithin(uint32_t a, uint32_t len, uint32_t base, uint32_t size)
{
    uint32_t end = a + len;
    if (end < a) /* address overflow */
    {
        return false;
    }
    return (a >= base) && (end <= base + size);
}

bool gameCanWrite(const void *p, uint32_t len)
{
    const uint32_t a = (uint32_t)p;
    if (len == 0U)
    {
        return true;
    }
    return rangeWithin(a, len, (uint32_t)&__game_ram_start, (uint32_t)&__game_ram_size) ||
           rangeWithin(a, len, (uint32_t)&__game_ram_asset_start, (uint32_t)&__game_ram_asset_size);
}

bool gameCanRead(const void *p, uint32_t len)
{
    const uint32_t a = (uint32_t)p;
    if (len == 0U)
    {
        return true;
    }
    return gameCanWrite(p, len) ||
           rangeWithin(a, len, (uint32_t)&__console_flash_start, (uint32_t)&__console_flash_size);
}

bool gameCopyStringIn(char *dst, uint32_t dst_size, const char *src)
{
    if (dst_size == 0U)
    {
        return false;
    }
    for (uint32_t i = 0U; i < dst_size - 1U; i++)
    {
        if (!gameCanRead(src + i, 1U))
        {
            return false;
        }
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return true;
        }
    }
    dst[dst_size - 1U] = '\0';
    return true;
}

/* Packed pixel / palette extents the renderer will read for one sprite. */
static uint32_t spritePixelBytes(const Sprite *s)
{
    const uint32_t bpp = (s->flags & SPRITE_IS_FMT_4BPP) ? 4U : 2U;
    return ((uint32_t)s->w * (uint32_t)s->h * bpp + 7U) / 8U;
}

static uint32_t spritePaletteBytes(const Sprite *s)
{
    const uint32_t slots = (s->flags & SPRITE_IS_FMT_4BPP) ? 16U : 4U;
    return slots * sizeof(uint16_t);
}

bool spritesValid(const Sprite *sprites, uint16_t count)
{
    if (!gameCanRead(sprites, (uint32_t)count * sizeof(Sprite)))
    {
        return false;
    }
    for (uint16_t i = 0U; i < count; i++)
    {
        if (!gameCanRead(sprites[i].pixels, spritePixelBytes(&sprites[i])) ||
            !gameCanRead(sprites[i].palette, spritePaletteBytes(&sprites[i])))
        {
            return false;
        }
    }
    return true;
}
