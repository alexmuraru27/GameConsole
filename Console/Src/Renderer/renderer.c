#include "renderer.h"
#include "string.h"
#include "ILI9341.h"

#define RENDERER_WIDTH 256U  // 32
#define RENDERER_HEIGHT 240U // 30
#define RENDERER_TILE_SCREEN_SIZE 8U
#define RENDERER_TILE_MEMORY_SIZE 16U

#define RENDERER_NAME_TABLE_SIZE ((RENDERER_WIDTH / RENDERER_TILE_SCREEN_SIZE) * (RENDERER_HEIGHT / RENDERER_TILE_SCREEN_SIZE))

#define RENDERER_ATTRIBUTE_TABLE_CLUSTERING_SIZE 4U
#define RENDERER_ATTRIBUTE_TABLE_SIZE (((RENDERER_WIDTH / RENDERER_TILE_SCREEN_SIZE) * (RENDERER_HEIGHT / RENDERER_TILE_SCREEN_SIZE)) / RENDERER_ATTRIBUTE_TABLE_CLUSTERING_SIZE)

// Tiles stored as bits for dirty flags - 0 do not render/ 1 to render
#define RENDERER_DIRTY_TILES_CLUSTERING_SIZE 8U
#define RENDERER_DIRTY_TILES_SIZE ((RENDERER_WIDTH / RENDERER_TILE_SCREEN_SIZE) * (RENDERER_HEIGHT / RENDERER_TILE_SCREEN_SIZE) / RENDERER_DIRTY_TILES_CLUSTERING_SIZE)

#define RENDERER_PATTERN_TABLE_SIZE 256U

#define RENDERER_OAM_MAX_SPRITE_SIZE 128U

#define RENDERER_SYSTEM_PALETTE_SIZE 64U

#define RENDERER_FRAME_PALETTE_SIZE 4U

#define RENDERER_FRAME_SUBPALETTE_SIZE 4U

// System colors from 0x00-0x3F = 64 colors. RGB565
const uint16_t s_system_palette[RENDERER_SYSTEM_PALETTE_SIZE] = {
    RGB2COLOR(98, 98, 98),    // 0
    RGB2COLOR(0, 46, 152),    // 1
    RGB2COLOR(12, 17, 194),   // 2
    RGB2COLOR(59, 0, 194),    // 3
    RGB2COLOR(101, 0, 152),   // 4
    RGB2COLOR(125, 0, 78),    // 5
    RGB2COLOR(125, 0, 0),     // 6
    RGB2COLOR(101, 25, 0),    // 7
    RGB2COLOR(59, 54, 0),     // 8
    RGB2COLOR(12, 79, 0),     // 9
    RGB2COLOR(0, 91, 0),      // 10
    RGB2COLOR(0, 89, 0),      // 11
    RGB2COLOR(0, 73, 78),     // 12
    RGB2COLOR(0, 0, 0),       // 13
    RGB2COLOR(0, 0, 0),       // 14
    RGB2COLOR(0, 0, 0),       // 15
    RGB2COLOR(171, 171, 171), // 16
    RGB2COLOR(0, 100, 243),   // 17
    RGB2COLOR(53, 60, 255),   // 18
    RGB2COLOR(118, 27, 255),  // 19
    RGB2COLOR(174, 10, 243),  // 20
    RGB2COLOR(206, 13, 143),  // 21
    RGB2COLOR(206, 35, 28),   // 22
    RGB2COLOR(174, 71, 0),    // 23
    RGB2COLOR(118, 111, 0),   // 24
    RGB2COLOR(53, 114, 0),    // 25
    RGB2COLOR(0, 161, 0),     // 26
    RGB2COLOR(0, 158, 28),    // 27
    RGB2COLOR(0, 136, 143),   // 28
    RGB2COLOR(0, 0, 0),       // 29
    RGB2COLOR(0, 0, 0),       // 30
    RGB2COLOR(0, 0, 0),       // 31
    RGB2COLOR(255, 255, 255), // 32
    RGB2COLOR(76, 181, 255),  // 33
    RGB2COLOR(133, 140, 255), // 34
    RGB2COLOR(200, 107, 255), // 35
    RGB2COLOR(255, 89, 255),  // 36
    RGB2COLOR(255, 92, 225),  // 37
    RGB2COLOR(255, 115, 107), // 38
    RGB2COLOR(255, 152, 5),   // 39
    RGB2COLOR(200, 192, 0),   // 40
    RGB2COLOR(133, 226, 0),   // 41
    RGB2COLOR(76, 244, 5),    // 42
    RGB2COLOR(43, 241, 107),  // 43
    RGB2COLOR(43, 218, 225),  // 44
    RGB2COLOR(78, 78, 78),    // 45
    RGB2COLOR(0, 0, 0),       // 46
    RGB2COLOR(0, 0, 0),       // 47
    RGB2COLOR(255, 255, 255), // 48
    RGB2COLOR(184, 255, 255), // 49
    RGB2COLOR(206, 209, 255), // 50
    RGB2COLOR(232, 196, 255), // 51
    RGB2COLOR(255, 189, 255), // 52
    RGB2COLOR(255, 190, 243), // 53
    RGB2COLOR(255, 199, 196), // 54
    RGB2COLOR(255, 214, 156), // 55
    RGB2COLOR(232, 230, 132), // 56
    RGB2COLOR(206, 243, 132), // 57
    RGB2COLOR(184, 250, 156), // 58
    RGB2COLOR(171, 249, 196), // 59
    RGB2COLOR(171, 240, 243), // 60
    RGB2COLOR(184, 184, 184), // 61
    RGB2COLOR(0, 0, 0),       // 62
    RGB2COLOR(0, 0, 0)        // 63
};

// TODO place renderer in CCRAM

// Holds texture data loaded from the game memory aka CHR
static uint8_t s_pattern_table[RENDERER_PATTERN_TABLE_SIZE][RENDERER_TILE_MEMORY_SIZE];

// Holds texture data indexes for background from pattern table
static uint8_t s_name_table[RENDERER_NAME_TABLE_SIZE];

// Holds palette data for background subdivided in 4x4 tiles
static uint8_t s_attribute_table[RENDERER_ATTRIBUTE_TABLE_SIZE];

// Object attribute memory
// Priority (0: in front of background; 1: behind background)
// isDirty - 1 if sprite needs to be redrawn
// [X pos][Filp_V Flip_H Priority isDirty 0 0 IdxPalette1 IdxPalette0][Tile idx][Y Pos]
static uint32_t s_oam[RENDERER_OAM_MAX_SPRITE_SIZE];

// Frame pallete for sprites. Contains indexes for SystemPalette
static uint16_t s_frame_palette_sprite[RENDERER_FRAME_PALETTE_SIZE][RENDERER_FRAME_SUBPALETTE_SIZE];

// Frame pallete for background. Contains indexes for SystemPalette
static uint16_t s_frame_palette_bg[RENDERER_FRAME_PALETTE_SIZE][RENDERER_FRAME_SUBPALETTE_SIZE];

// Background tile positions that need to be redrawn
static uint8_t s_dirtyTiles[RENDERER_DIRTY_TILES_SIZE];

void rendererInit(void)
{
    memset(&s_pattern_table, 0U, sizeof(s_pattern_table));
    memset(&s_name_table, 0U, sizeof(s_name_table));
    memset(&s_attribute_table, 0U, sizeof(s_attribute_table));
    memset(&s_oam, 0U, sizeof(s_oam));
    memset(&s_frame_palette_sprite, 0U, sizeof(s_frame_palette_sprite));
    memset(&s_frame_palette_bg, 0U, sizeof(s_frame_palette_bg));
    memset(&s_dirtyTiles, 0U, sizeof(s_dirtyTiles));
}

// TODO draw no transparency - >can be faster due to less overhead on spi with window setting
static void drawSprite(const uint8_t x, const uint8_t y, const uint8_t sprite_index, const uint8_t frame_pallete_idx)
{
    if (frame_pallete_idx < RENDERER_FRAME_PALETTE_SIZE)
    {
        uint8_t color_index = 0U;
        for (uint8_t row = 0U; row < RENDERER_TILE_SCREEN_SIZE; row++)
        {
            for (uint8_t col = 0U; col < RENDERER_TILE_SCREEN_SIZE; col++)
            {
                color_index = (((s_pattern_table[sprite_index][row + RENDERER_TILE_SCREEN_SIZE] >> (RENDERER_TILE_SCREEN_SIZE - 1U - col)) & 1) << 1) |
                              ((s_pattern_table[sprite_index][row] >> (RENDERER_TILE_SCREEN_SIZE - 1U - col)) & 1);
                if (color_index != 0U)
                {
                    ili9341DrawPixel(x + col, y + row, s_frame_palette_sprite[frame_pallete_idx][color_index]);
                }
            }
        }
    }
}

void rendererRender(void)
{
    // TODO dirty checker
    // TODO overlap checker
    // draw the system pallete
    const uint16_t TILE_SIZE = 20U;
    for (int i = 0; i < RENDERER_SYSTEM_PALETTE_SIZE; ++i)
    {
        int row = i / (320 / TILE_SIZE); // how many fit per row
        int col = i % (320 / TILE_SIZE);

        int x = col * TILE_SIZE;
        int y = row * TILE_SIZE;

        ili9341FillRectangle(x, y, TILE_SIZE, TILE_SIZE, s_system_palette[i]);
    }

    drawSprite(80U, 90U, 0U, 2U);

    const uint8_t FRAME_PALETTE_IDX = 0U;
    drawSprite(100U, 100U, 2U, FRAME_PALETTE_IDX);
    drawSprite(100U + RENDERER_TILE_SCREEN_SIZE, 100U, 3U, FRAME_PALETTE_IDX);
    drawSprite(100U, 100U + RENDERER_TILE_SCREEN_SIZE, 4U, FRAME_PALETTE_IDX);
    drawSprite(100U + RENDERER_TILE_SCREEN_SIZE, 100U + RENDERER_TILE_SCREEN_SIZE, 5U, FRAME_PALETTE_IDX);
}

void rendererPaletteSetSprite(const uint8_t pallete_index, const uint8_t color_index, const uint8_t system_pallete_index)
{
    if ((pallete_index < RENDERER_FRAME_PALETTE_SIZE) && (color_index < RENDERER_FRAME_SUBPALETTE_SIZE) && (color_index != 0U))
    {
        s_frame_palette_sprite[pallete_index][color_index] = s_system_palette[system_pallete_index];
    }
}

void rendererPaletteSetBackground(const uint8_t pallete_index, const uint8_t color_index, const uint8_t system_pallete_index)
{
    if ((pallete_index < RENDERER_FRAME_PALETTE_SIZE) && (color_index < RENDERER_FRAME_SUBPALETTE_SIZE) && (color_index != 0U))
    {
        s_frame_palette_bg[pallete_index][color_index] = s_system_palette[system_pallete_index];
    }
}

void rendererPatternTableSetTile(const uint8_t table_index, const uint8_t *tile_data, const uint8_t tile_size)
{
    if ((tile_size == RENDERER_TILE_MEMORY_SIZE) && (table_index < RENDERER_PATTERN_TABLE_SIZE))
    {
        for (int i = 0U; i < RENDERER_TILE_MEMORY_SIZE; ++i)
        {
            s_pattern_table[table_index][i] = tile_data[i];
        }
    }
}

void rendererPatternTableClear(const uint8_t system_color)
{
    memset(&s_pattern_table, system_color, sizeof(s_pattern_table));
}

void rendererTriggerCompleteRedraw(void)
{
    memset(&s_dirtyTiles, 0xFFU, RENDERER_DIRTY_TILES_SIZE);
}

void rendererNameTableSetTile(uint8_t table_index, uint8_t tile_idx)
{
    if (table_index < RENDERER_NAME_TABLE_SIZE)
    {
        s_name_table[table_index] = tile_idx;
    }
}
void rendererNameTableClear()
{
    memset(&s_name_table, 0U, sizeof(s_name_table));
}