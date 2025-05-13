#include "renderer.h"
#include "string.h"
#include "ILI9341.h"

#define RENDERER_WIDTH 320U
#define RENDERER_HEIGHT 240U
#define RENDERER_TILE_SIZE 16U
#define RENDERER_NAME_TABLE_SIZE ((RENDERER_WIDTH / RENDERER_TILE_SIZE) * (RENDERER_HEIGHT / RENDERER_TILE_SIZE))
#define RENDERER_ATTRIBUTE_TABLE_SIZE (((RENDERER_WIDTH / RENDERER_TILE_SIZE) * (RENDERER_HEIGHT / RENDERER_TILE_SIZE)) / 4U)

// Tiles stored as bits for dirty flags - 0 do not render/ 1 to render
#define RENDERER_DIRTY_TILES_SIZE ((RENDERER_WIDTH / RENDERER_TILE_SIZE) * (RENDERER_HEIGHT / RENDERER_TILE_SIZE) / 8U)
#define RENDERER_PATTERN_TABLE_SIZE 256U
#define RENDERER_OAM_SIZE 64U
#define RENDERER_OAM_SPRITE_DATA_SIZE 4U
#define RENDERER_SYSTEM_PALETTE_SIZE 64U
#define RENDERER_SELECTED_PALETTE_SIZE 16U

static uint8_t s_name_table[RENDERER_NAME_TABLE_SIZE];
static uint8_t s_attribute_table[RENDERER_ATTRIBUTE_TABLE_SIZE];
static uint8_t s_pattern_table[RENDERER_PATTERN_TABLE_SIZE];
static uint8_t s_oam[RENDERER_OAM_SIZE][RENDERER_OAM_SPRITE_DATA_SIZE];
static uint8_t s_dirtyTiles[RENDERER_DIRTY_TILES_SIZE];
static uint16_t s_system_palette[RENDERER_SYSTEM_PALETTE_SIZE];
static uint8_t s_frame_palette_sprite[RENDERER_SELECTED_PALETTE_SIZE];
static uint8_t s_frame_palette_bg[RENDERER_SELECTED_PALETTE_SIZE];

void rendererInit(void)
{
}

void rendererRender(void)
{
}

void rendererSetPaletteSystem(const uint16_t *rgb_565_color_buffer, const uint8_t size)
{
    if (size == RENDERER_SYSTEM_PALETTE_SIZE)
    {
        for (uint8_t i = 0U; i < size; i++)
        {
            s_system_palette[i] = rgb_565_color_buffer[i];
        }
    }
}

void rendererSetPaletteSprite(const uint8_t pallete_index, const uint8_t color_index, const uint8_t system_pallete_index)
{
    if (pallete_index < 4U && color_index < 4U)
    {
        s_frame_palette_sprite[pallete_index * 4U + color_index] = system_pallete_index;
    }
}

void rendererSetPaletteBackground(const uint8_t pallete_index, const uint8_t color_index, const uint8_t system_pallete_index)
{
    if (pallete_index < 4U && color_index < 4U)
    {
        s_frame_palette_bg[pallete_index * 4U + color_index] = system_pallete_index;
    }
}

void rendererTriggerCompleteRedraw(void)
{
    memset(&s_dirtyTiles, 0xFFU, RENDERER_DIRTY_TILES_SIZE);
}