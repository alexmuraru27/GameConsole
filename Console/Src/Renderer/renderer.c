#include "renderer.h"
#include "string.h"
#include "ILI9341.h"
#define CCMRAM __attribute__((section(".ccmram")))

#define RENDERER_WIDTH 256U  // 32
#define RENDERER_HEIGHT 240U // 30
#define RENDERER_TILE_SCREEN_SIZE 16U
#define RENDERER_TILE_MEMORY_SIZE 64U
#define RENDERER_TILES_IN_ROW (RENDERER_WIDTH / RENDERER_TILE_SCREEN_SIZE)
#define RENDERER_TILES_IN_COLUMN (RENDERER_HEIGHT / RENDERER_TILE_SCREEN_SIZE)
#define RENDERER_HELPER_TILE_COORD_TO_INDEX(x, y) ((y) * RENDERER_TILES_IN_ROW + (x))

#define RENDERER_TILE_ROW_BYTES (RENDERER_TILE_SCREEN_SIZE / 8U)

#define RENDERER_NAME_TABLE_SIZE (RENDERER_TILES_IN_ROW * RENDERER_TILES_IN_COLUMN)

// Attribute table needs 4bits for each tile since we are using 16 frame palettes for bg
// [0 0 Filp_V Flip_H IdxPalette3 IdxPalette2 IdxPalette1 IdxPalette0]
#define RENDERER_ATTRIBUTE_TABLE_PALETTE_POS 0U
#define RENDERER_ATTRIBUTE_TABLE_PALETTE_MASK 0x0FU

#define RENDERER_ATTRIBUTE_TABLE_FLIP_H_POS 4U
#define RENDERER_ATTRIBUTE_TABLE_FLIP_H_MASK 0x01U

#define RENDERER_ATTRIBUTE_TABLE_FLIP_V_POS 5U
#define RENDERER_ATTRIBUTE_TABLE_FLIP_V_MASK 0x01U
#define RENDERER_ATTRIBUTE_TABLE_SIZE RENDERER_NAME_TABLE_SIZE

#define RENDERER_DIRTY_TILES_SIZE RENDERER_NAME_TABLE_SIZE

#define RENDERER_PATTERN_TABLE_SIZE 256U

#define RENDERER_OAM_SIZE 64U

#define RENDERER_SYSTEM_PALETTE_SIZE 64U

#define RENDERER_FRAME_PALETTE_SIZE 16U

#define RENDERER_FRAME_SUBPALETTE_SIZE 4U

// Priority (0: in front of background; 1: behind background)
// isDirty - 1 if sprite needs to be redrawn
// as bits 2-4 were not used inside byte 3, we will use them to extend the palette information to 4 bits = max 16 sprite palettes per frame
// also we will add a dirty flag
// [X pos][Filp_V Flip_H Priority isDirty IdxPalette3 IdxPalette2 IdxPalette1 IdxPalette0][Tile idx][Y Pos]
#define RENDERER_OAM_X_POS 24U
#define RENDERER_OAM_X_MASK 0xFFU

#define RENDERER_OAM_FLIP_V_POS 23U
#define RENDERER_OAM_FLIP_V_MASK 0x01U

#define RENDERER_OAM_FLIP_H_POS 22U
#define RENDERER_OAM_FLIP_H_MASK 0x01U

#define RENDERER_OAM_PRIORITY_POS 21U
#define RENDERER_OAM_PRIORITY_MASK 0x01U

#define RENDERER_OAM_IS_DIRTY_POS 20U
#define RENDERER_OAM_IS_DIRTY_MASK 0x01U

#define RENDERER_OAM_PALETTE_IDX_POS 16U
#define RENDERER_OAM_PALETTE_IDX_MASK 0x0FU

#define RENDERER_OAM_TILE_IDX_POS 8U
#define RENDERER_OAM_TILE_IDX_MASK 0xFFU

#define RENDERER_OAM_Y_POS 0U
#define RENDERER_OAM_Y_MASK 0xFFU

#define RENDERER_DIRTY_FLAG_CLEAR 0U
#define RENDERER_DIRTY_FLAG_OLD_SET 1U
#define RENDERER_DIRTY_FLAG_NEW_SET 2U

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

// Holds texture data loaded from the game memory aka CHR
static CCMRAM uint8_t s_pattern_table[RENDERER_PATTERN_TABLE_SIZE][RENDERER_TILE_MEMORY_SIZE];

// Holds texture data indexes for background from pattern table
static CCMRAM uint8_t s_name_table[RENDERER_NAME_TABLE_SIZE];

// Holds palette data for background - each tile has its own palette
static CCMRAM uint8_t s_attribute_table[RENDERER_ATTRIBUTE_TABLE_SIZE];

// Object attribute memory
static CCMRAM uint32_t s_oam[RENDERER_OAM_SIZE];

// Frame palette for sprites. Contains indexes for SystemPalette
static CCMRAM uint16_t s_frame_palette_sprite[RENDERER_FRAME_PALETTE_SIZE][RENDERER_FRAME_SUBPALETTE_SIZE];

// Frame palette for background. Contains indexes for SystemPalette
static CCMRAM uint16_t s_frame_palette_bg[RENDERER_FRAME_PALETTE_SIZE][RENDERER_FRAME_SUBPALETTE_SIZE];

// Background tile positions that need to be redrawn
static CCMRAM uint8_t s_dirtyTiles[RENDERER_DIRTY_TILES_SIZE];

void rendererInit(void)
{
    memset(&s_pattern_table, 0U, sizeof(s_pattern_table));
    memset(&s_name_table, 0U, sizeof(s_name_table));
    memset(&s_attribute_table, 0U, sizeof(s_attribute_table));
    memset(&s_oam, 0U, sizeof(s_oam));
    memset(&s_frame_palette_sprite, 0U, sizeof(s_frame_palette_sprite));
    memset(&s_frame_palette_bg, 0U, sizeof(s_frame_palette_bg));
    memset(&s_dirtyTiles, RENDERER_DIRTY_FLAG_CLEAR, sizeof(s_dirtyTiles));
}

static uint8_t xyCoordsToTileIndexMap(const uint8_t screen_x, const uint8_t screen_y)
{
    if ((screen_x < RENDERER_WIDTH) && (screen_y < RENDERER_HEIGHT))
    {
        uint8_t tile_x = screen_x / RENDERER_TILE_SCREEN_SIZE;
        uint8_t tile_y = screen_y / RENDERER_TILE_SCREEN_SIZE;

        if (tile_x >= RENDERER_TILES_IN_ROW)
            tile_x = RENDERER_TILES_IN_ROW - 1;
        if (tile_y >= RENDERER_TILES_IN_COLUMN)
            tile_y = RENDERER_TILES_IN_COLUMN - 1;

        return RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y);
    }
    return 0U;
}

static void rendererSetDirtyBgTilesTouchedBySprite(const uint8_t sprite_x, const uint8_t sprite_y)
{
    if ((sprite_x < (RENDERER_WIDTH - RENDERER_TILE_SCREEN_SIZE)) && (sprite_y < (RENDERER_HEIGHT - RENDERER_TILE_SCREEN_SIZE)))
    {
        uint8_t tile_x_0 = sprite_x / RENDERER_TILE_SCREEN_SIZE;
        uint8_t tile_y_0 = sprite_y / RENDERER_TILE_SCREEN_SIZE;
        uint8_t tile_x_1 = (sprite_x + RENDERER_TILE_SCREEN_SIZE - 1U) / RENDERER_TILE_SCREEN_SIZE;
        uint8_t tile_y_1 = (sprite_y + RENDERER_TILE_SCREEN_SIZE - 1U) / RENDERER_TILE_SCREEN_SIZE;

        // x0-y0
        uint8_t index = xyCoordsToTileIndexMap(tile_x_0 * RENDERER_TILE_SCREEN_SIZE, tile_y_0 * RENDERER_TILE_SCREEN_SIZE);
        s_dirtyTiles[index] = RENDERER_DIRTY_FLAG_NEW_SET;

        // x1-y0
        if (tile_x_1 != tile_x_0)
        {
            index = xyCoordsToTileIndexMap(tile_x_1 * RENDERER_TILE_SCREEN_SIZE, tile_y_0 * RENDERER_TILE_SCREEN_SIZE);
            s_dirtyTiles[index] = RENDERER_DIRTY_FLAG_NEW_SET;
        }

        // x0-y1
        if (tile_y_1 != tile_y_0)
        {
            index = xyCoordsToTileIndexMap(tile_x_0 * RENDERER_TILE_SCREEN_SIZE, tile_y_1 * RENDERER_TILE_SCREEN_SIZE);
            s_dirtyTiles[index] = RENDERER_DIRTY_FLAG_NEW_SET;

            // x1-y1
            if (tile_x_1 != tile_x_0)
            {
                index = xyCoordsToTileIndexMap(tile_x_1 * RENDERER_TILE_SCREEN_SIZE, tile_y_1 * RENDERER_TILE_SCREEN_SIZE);
                s_dirtyTiles[index] = RENDERER_DIRTY_FLAG_NEW_SET;
            }
        }
    }
}

static void rendererSetDirtyBgTiles(const uint8_t nametable_idx)
{
    if (nametable_idx < RENDERER_DIRTY_TILES_SIZE)
    {
        s_dirtyTiles[nametable_idx] = RENDERER_DIRTY_FLAG_NEW_SET;
    }
}

static void rendererAttributeTableSetPaletteIdx(const uint8_t name_table_idx, const uint8_t palette)
{
    if ((name_table_idx < (RENDERER_ATTRIBUTE_TABLE_SIZE)) && (palette < RENDERER_FRAME_PALETTE_SIZE))
    {
        s_attribute_table[name_table_idx] &= ~(RENDERER_ATTRIBUTE_TABLE_PALETTE_MASK << RENDERER_ATTRIBUTE_TABLE_PALETTE_POS);
        s_attribute_table[name_table_idx] |= ((palette & RENDERER_ATTRIBUTE_TABLE_PALETTE_MASK) << RENDERER_ATTRIBUTE_TABLE_PALETTE_POS);
        rendererSetDirtyBgTiles(name_table_idx);
    }
}

static uint8_t rendererAttributeTableGetPaletteIdx(const uint8_t name_table_idx)
{
    if (name_table_idx < (RENDERER_ATTRIBUTE_TABLE_SIZE))
    {
        return ((s_attribute_table[name_table_idx] & (RENDERER_ATTRIBUTE_TABLE_PALETTE_MASK << RENDERER_ATTRIBUTE_TABLE_PALETTE_POS)) >> RENDERER_ATTRIBUTE_TABLE_PALETTE_POS);
    }
    return 0U;
}

static void rendererAttributeTableSetFlipVIdx(const uint8_t name_table_idx, const bool isFlipV)
{
    if (name_table_idx < (RENDERER_ATTRIBUTE_TABLE_SIZE))
    {
        s_attribute_table[name_table_idx] &= ~(RENDERER_ATTRIBUTE_TABLE_FLIP_V_MASK << RENDERER_ATTRIBUTE_TABLE_FLIP_V_POS);
        if (isFlipV)
        {
            s_attribute_table[name_table_idx] |= (RENDERER_ATTRIBUTE_TABLE_FLIP_V_MASK << RENDERER_ATTRIBUTE_TABLE_FLIP_V_POS);
        }
        rendererSetDirtyBgTiles(name_table_idx);
    }
}

static bool rendererAttributeTableGetFlipVIdx(const uint8_t name_table_idx)
{
    if (name_table_idx < (RENDERER_ATTRIBUTE_TABLE_SIZE))
    {
        return ((s_attribute_table[name_table_idx] & (RENDERER_ATTRIBUTE_TABLE_FLIP_V_MASK << RENDERER_ATTRIBUTE_TABLE_FLIP_V_POS)) != 0U);
    }
    return false;
}

static void rendererAttributeTableSetFlipHIdx(const uint8_t name_table_idx, const bool isFlipH)
{
    if (name_table_idx < (RENDERER_ATTRIBUTE_TABLE_SIZE))
    {
        s_attribute_table[name_table_idx] &= ~(RENDERER_ATTRIBUTE_TABLE_FLIP_H_MASK << RENDERER_ATTRIBUTE_TABLE_FLIP_H_POS);
        if (isFlipH)
        {
            s_attribute_table[name_table_idx] |= (RENDERER_ATTRIBUTE_TABLE_FLIP_H_MASK << RENDERER_ATTRIBUTE_TABLE_FLIP_H_POS);
        }
        rendererSetDirtyBgTiles(name_table_idx);
    }
}

static bool rendererAttributeTableGetFlipHIdx(const uint8_t name_table_idx)
{
    if (name_table_idx < (RENDERER_NAME_TABLE_SIZE))
    {
        return ((s_attribute_table[name_table_idx] & (RENDERER_ATTRIBUTE_TABLE_FLIP_H_MASK << RENDERER_ATTRIBUTE_TABLE_FLIP_H_POS)) != 0U);
    }
    return false;
}

static void drawTile(const uint8_t x, const uint8_t y, bool is_bg, const uint8_t pattern_index, const uint8_t palette_idx, bool is_flip_h, bool is_flip_v)
{
    if (palette_idx >= RENDERER_FRAME_PALETTE_SIZE || pattern_index >= RENDERER_PATTERN_TABLE_SIZE)
    {
        return;
    }

    const uint16_t *palette = is_bg ? s_frame_palette_bg[palette_idx] : s_frame_palette_sprite[palette_idx];

    // tile fully opaque -> all pixels  color_idx != 0
    bool tile_fully_opaque = true;
    for (uint8_t row = 0U; row < RENDERER_TILE_SCREEN_SIZE; row++)
    {
        // Use flipped row index for vertical flip
        uint8_t row_idx = is_flip_v ? (RENDERER_TILE_SCREEN_SIZE - 1 - row) : row;
        uint8_t byte0_low = s_pattern_table[pattern_index][row_idx * RENDERER_TILE_ROW_BYTES];
        uint8_t byte0_high = s_pattern_table[pattern_index][row_idx * RENDERER_TILE_ROW_BYTES + 1U];
        uint8_t byte1_low = s_pattern_table[pattern_index][RENDERER_TILE_ROW_BYTES * RENDERER_TILE_SCREEN_SIZE + row_idx * RENDERER_TILE_ROW_BYTES];
        uint8_t byte1_high = s_pattern_table[pattern_index][RENDERER_TILE_ROW_BYTES * RENDERER_TILE_SCREEN_SIZE + row_idx * RENDERER_TILE_ROW_BYTES + 1U];
        if (((byte0_low | byte1_low) != 0xFF) || ((byte0_high | byte1_high) != 0xFF))
        {
            tile_fully_opaque = false;
            break;
        }
    }

    // if fully opaque, send it in one burst
    if (tile_fully_opaque)
    {
        ili9341SetAddrWindow(x, y, RENDERER_TILE_SCREEN_SIZE, RENDERER_TILE_SCREEN_SIZE);
        for (uint8_t row = 0U; row < RENDERER_TILE_SCREEN_SIZE; row++)
        {
            // use flipped row index for vertical flip
            uint8_t row_idx = is_flip_v ? (RENDERER_TILE_SCREEN_SIZE - 1 - row) : row;
            uint8_t byte0_low = s_pattern_table[pattern_index][row_idx * RENDERER_TILE_ROW_BYTES];
            uint8_t byte0_high = s_pattern_table[pattern_index][row_idx * RENDERER_TILE_ROW_BYTES + 1];
            uint8_t byte1_low = s_pattern_table[pattern_index][RENDERER_TILE_SCREEN_SIZE * RENDERER_TILE_ROW_BYTES + row_idx * RENDERER_TILE_ROW_BYTES];
            uint8_t byte1_high = s_pattern_table[pattern_index][RENDERER_TILE_SCREEN_SIZE * RENDERER_TILE_ROW_BYTES + row_idx * RENDERER_TILE_ROW_BYTES + 1];

            // process all 16 pixels, reversing order for horizontal flip
            for (uint8_t col = 0U; col < RENDERER_TILE_SCREEN_SIZE; col++)
            {
                // map column: 0->15, 1->14, ..., 15->0 when flipped
                uint8_t col_idx = is_flip_h ? (RENDERER_TILE_SCREEN_SIZE - 1 - col) : col;
                // determine which byte and bit (0-7 for low, 8-15 for high)
                uint8_t byte_idx = col_idx / 8U;
                uint8_t bit_pos = 7U - (col_idx % 8U);
                // select correct byte for each bitplane
                uint8_t byte0 = (byte_idx == 0) ? byte0_low : byte0_high;
                uint8_t byte1 = (byte_idx == 0) ? byte1_low : byte1_high;
                uint8_t color_idx = (((byte1 >> bit_pos) & 1U) << 1U) | ((byte0 >> bit_pos) & 1U);
                ili9341SendPixel(palette[color_idx]);
            }
        }
        return;
    }

    // if not fully opaque, send each row individually
    for (uint8_t row = 0U; row < RENDERER_TILE_SCREEN_SIZE; row++)
    {
        // use flipped row index for vertical flip
        uint8_t row_idx = is_flip_v ? (RENDERER_TILE_SCREEN_SIZE - 1 - row) : row;
        uint8_t byte0_low = s_pattern_table[pattern_index][row_idx * RENDERER_TILE_ROW_BYTES];
        uint8_t byte0_high = s_pattern_table[pattern_index][row_idx * RENDERER_TILE_ROW_BYTES + 1U];
        uint8_t byte1_low = s_pattern_table[pattern_index][RENDERER_TILE_SCREEN_SIZE * RENDERER_TILE_ROW_BYTES + row_idx * RENDERER_TILE_ROW_BYTES];
        uint8_t byte1_high = s_pattern_table[pattern_index][RENDERER_TILE_SCREEN_SIZE * RENDERER_TILE_ROW_BYTES + row_idx * RENDERER_TILE_ROW_BYTES + 1U];

        // skip fully transparent rows
        if ((byte0_low == 0U && byte1_low == 0U) && (byte0_high == 0U && byte1_high == 0U))
        {
            continue;
        }

        // compute colors and transparency for the row
        uint16_t row_colors[RENDERER_TILE_SCREEN_SIZE];
        bool is_transparent[RENDERER_TILE_SCREEN_SIZE];
        for (uint8_t col = 0U; col < RENDERER_TILE_SCREEN_SIZE; col++)
        {
            // map column: 0->15, 1->14, ..., 15->0 when flipped
            uint8_t col_idx = is_flip_h ? (RENDERER_TILE_SCREEN_SIZE - 1 - col) : col;
            // determine which byte and bit
            uint8_t byte_idx = col_idx / 8U;
            uint8_t bit_pos = 7U - (col_idx % 8U);
            // select correct byte for each bitplane
            uint8_t byte0 = (byte_idx == 0) ? byte0_low : byte0_high;
            uint8_t byte1 = (byte_idx == 0) ? byte1_low : byte1_high;
            uint8_t color_idx = (((byte1 >> bit_pos) & 1U) << 1U) | ((byte0 >> bit_pos) & 1U);
            row_colors[col] = palette[color_idx];
            is_transparent[col] = (color_idx == 0U);
        }

        // send bursts of opaque pixels
        // basically we just parse the whole row
        uint8_t col_idx = 0;
        while (col_idx < RENDERER_TILE_SCREEN_SIZE)
        {
            // for transparent pixels, we skip them
            if (is_transparent[col_idx])
            {
                col_idx++;
                continue;
            }

            // if we have multiple un-transparent pixels, we group them up
            uint8_t burst_start_idx = col_idx;
            uint8_t burst_length = 1;
            while ((col_idx + burst_length) < RENDERER_TILE_SCREEN_SIZE && !is_transparent[col_idx + burst_length])
            {
                burst_length++;
            }

            if (burst_length == 1)
            {
                ili9341DrawPixel(x + burst_start_idx, y + row, row_colors[burst_start_idx]);
            }
            else
            {
                ili9341SetAddrWindow(x + burst_start_idx, y + row, burst_length, 1);
                for (uint8_t i = 0; i < burst_length; i++)
                {
                    ili9341SendPixel(row_colors[burst_start_idx + i]);
                }
            }

            col_idx += burst_length;
        }
    }
}

void rendererRender(void)
{
    // TODO dirty checker
    // TODO overlap checker

    // 1. render OAM with priority=1 -> back of BG
    for (uint8_t i = 0U; i < RENDERER_OAM_SIZE; i++)
    {
        const uint8_t tile_idx = rendererOamGetTileIdx(i);
        if ((tile_idx != 0U) && rendererOamGetPriority(i))
        {
            rendererOamSetIsDirty(i, false);
            const uint8_t x = rendererOamGetXPos(i);
            const uint8_t y = rendererOamGetYPos(i);
            const uint8_t palette = rendererOamGetPaletteIdx(i);
            const bool is_flip_v = rendererOamGetFlipV(i);
            const bool is_flip_h = rendererOamGetFlipH(i);

            drawTile(x, y, false, tile_idx, palette, is_flip_h, is_flip_v);
        }
    }

    // 2. render BG
    for (uint16_t i = 0U; i < RENDERER_NAME_TABLE_SIZE; i++)
    {
        if (s_name_table[i] != 0U)
        {
            drawTile(((i * RENDERER_TILE_SCREEN_SIZE) % RENDERER_WIDTH), ((i / RENDERER_TILES_IN_ROW) * RENDERER_TILE_SCREEN_SIZE), true, s_name_table[i], rendererAttributeTableGetPaletteIdx(i), rendererAttributeTableGetFlipHIdx(i), rendererAttributeTableGetFlipVIdx(i));
        }
    }

    // 3. render OAM with priority=0 -> front of BG
    for (uint8_t i = 0U; i < RENDERER_OAM_SIZE; i++)
    {
        const uint8_t tile_idx = rendererOamGetTileIdx(i);
        if ((tile_idx != 0U) && (!rendererOamGetPriority(i)))
        {
            rendererOamSetIsDirty(i, false);
            const uint8_t x = rendererOamGetXPos(i);
            const uint8_t y = rendererOamGetYPos(i);
            const uint8_t palette = rendererOamGetPaletteIdx(i);
            const bool is_flip_v = rendererOamGetFlipV(i);
            const bool is_flip_h = rendererOamGetFlipH(i);

            drawTile(x, y, false, tile_idx, palette, is_flip_h, is_flip_v);
        }
    }
}

void rendererFramePaletteSetSprite(const uint8_t palette_idx, const uint8_t color_idx, const uint8_t system_palette_idx)
{
    if ((palette_idx < RENDERER_FRAME_PALETTE_SIZE) && (color_idx < RENDERER_FRAME_SUBPALETTE_SIZE) && (color_idx != 0U))
    {
        s_frame_palette_sprite[palette_idx][color_idx] = s_system_palette[system_palette_idx];
    }
}

void rendererFramePaletteSetSpriteMultiple(const uint8_t palette_idx, const uint8_t system_palette_idx_1, const uint8_t system_palette_idx_2, const uint8_t system_palette_idx_3)
{
    if (
        (palette_idx < RENDERER_FRAME_PALETTE_SIZE) && (system_palette_idx_1 < RENDERER_SYSTEM_PALETTE_SIZE) && (system_palette_idx_2 < RENDERER_SYSTEM_PALETTE_SIZE) && (system_palette_idx_3 < RENDERER_SYSTEM_PALETTE_SIZE))
    {
        s_frame_palette_sprite[palette_idx][1U] = s_system_palette[system_palette_idx_1];
        s_frame_palette_sprite[palette_idx][2U] = s_system_palette[system_palette_idx_2];
        s_frame_palette_sprite[palette_idx][3U] = s_system_palette[system_palette_idx_3];
    }
}

void rendererFramePaletteSetBackground(const uint8_t palette_idx, const uint8_t color_idx, const uint8_t system_palette_idx)
{
    if ((palette_idx < RENDERER_FRAME_PALETTE_SIZE) && (color_idx < RENDERER_FRAME_SUBPALETTE_SIZE) && (color_idx != 0U))
    {
        s_frame_palette_bg[palette_idx][color_idx] = s_system_palette[system_palette_idx];
        rendererSetDirtyCompleteRedraw();
    }
}

void rendererFramePaletteSetBackgroundMultiple(const uint8_t palette_idx, const uint8_t system_palette_idx_1, const uint8_t system_palette_idx_2, const uint8_t system_palette_idx_3)
{
    if (
        (palette_idx < RENDERER_FRAME_PALETTE_SIZE) && (system_palette_idx_1 < RENDERER_SYSTEM_PALETTE_SIZE) && (system_palette_idx_2 < RENDERER_SYSTEM_PALETTE_SIZE) && (system_palette_idx_3 < RENDERER_SYSTEM_PALETTE_SIZE))
    {
        s_frame_palette_bg[palette_idx][1U] = s_system_palette[system_palette_idx_1];
        s_frame_palette_bg[palette_idx][2U] = s_system_palette[system_palette_idx_2];
        s_frame_palette_bg[palette_idx][3U] = s_system_palette[system_palette_idx_3];
        rendererSetDirtyCompleteRedraw();
    }
}

void rendererPatternTableSetTile(const uint8_t pattern_table_idx, const uint8_t *tile_data, const uint8_t tile_size)
{
    if ((tile_size == RENDERER_TILE_MEMORY_SIZE) && (pattern_table_idx < RENDERER_PATTERN_TABLE_SIZE) && (pattern_table_idx > 0U))
    {
        for (int i = 0U; i < RENDERER_TILE_MEMORY_SIZE; ++i)
        {
            s_pattern_table[pattern_table_idx][i] = tile_data[i];
        }
    }
}

void rendererPatternTableClear()
{
    memset(&s_pattern_table, 0U, sizeof(s_pattern_table));
}

void rendererSetDirtyCompleteRedraw(void)
{
    memset(&s_dirtyTiles, RENDERER_DIRTY_FLAG_NEW_SET, RENDERER_DIRTY_TILES_SIZE);
}

void rendererNameTableSetTile(uint8_t tile_x, uint8_t tile_y, uint8_t tile_idx)
{
    const uint8_t nametable_idx = RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y);
    if ((nametable_idx) < RENDERER_NAME_TABLE_SIZE)
    {
        s_name_table[nametable_idx] = tile_idx;
        rendererSetDirtyBgTiles(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y));
    }
}

void rendererOamClearEntry(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        s_oam[oam_idx] = 0U;
    }
}

uint8_t rendererOamGetXPos(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return (uint8_t)((s_oam[oam_idx] & (RENDERER_OAM_X_MASK << RENDERER_OAM_X_POS)) >> RENDERER_OAM_X_POS);
    }
    return 0U;
}

void rendererOamSetXYPos(const uint8_t oam_idx, const uint8_t x_pos, const uint8_t y_pos)
{
    if ((oam_idx < RENDERER_OAM_SIZE) && ((x_pos != rendererOamGetXPos(oam_idx)) || (y_pos != rendererOamGetYPos(oam_idx))))
    {
        // X Pos
        s_oam[oam_idx] &= ~(RENDERER_OAM_X_MASK << RENDERER_OAM_X_POS);
        s_oam[oam_idx] |= ((x_pos & RENDERER_OAM_X_MASK) << RENDERER_OAM_X_POS);

        // Y Pos
        s_oam[oam_idx] &= ~(RENDERER_OAM_Y_MASK << RENDERER_OAM_Y_POS);
        s_oam[oam_idx] |= ((y_pos & RENDERER_OAM_Y_MASK) << RENDERER_OAM_Y_POS);
        rendererOamSetIsDirty(oam_idx, true);
        rendererSetDirtyBgTilesTouchedBySprite(x_pos, y_pos);
    }
}

bool rendererOamGetFlipV(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return ((s_oam[oam_idx] & (RENDERER_OAM_FLIP_V_MASK << RENDERER_OAM_FLIP_V_POS)) >> RENDERER_OAM_FLIP_V_POS) != 0U;
    }
    return false;
}

void rendererOamSetFlipV(const uint8_t oam_idx, const bool is_flip_v)
{
    if (oam_idx < RENDERER_OAM_SIZE && (is_flip_v != rendererOamGetFlipV(oam_idx)))
    {
        if (is_flip_v)
        {
            s_oam[oam_idx] |= (RENDERER_OAM_FLIP_V_MASK << RENDERER_OAM_FLIP_V_POS);
        }
        else
        {

            s_oam[oam_idx] &= ~(RENDERER_OAM_FLIP_V_MASK << RENDERER_OAM_FLIP_V_POS);
        }
        rendererOamSetIsDirty(oam_idx, true);
    }
}

bool rendererOamGetFlipH(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return ((s_oam[oam_idx] & (RENDERER_OAM_FLIP_H_MASK << RENDERER_OAM_FLIP_H_POS)) >> RENDERER_OAM_FLIP_H_POS) != 0U;
    }
    return false;
}

void rendererOamSetFlipH(const uint8_t oam_idx, const bool is_flip_h)
{
    if (oam_idx < RENDERER_OAM_SIZE && (is_flip_h != rendererOamGetFlipH(oam_idx)))
    {
        if (is_flip_h)
        {
            s_oam[oam_idx] |= (RENDERER_OAM_FLIP_H_MASK << RENDERER_OAM_FLIP_H_POS);
        }
        else
        {

            s_oam[oam_idx] &= ~(RENDERER_OAM_FLIP_H_MASK << RENDERER_OAM_FLIP_H_POS);
        }
        rendererOamSetIsDirty(oam_idx, true);
    }
}

bool rendererOamGetPriority(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return ((s_oam[oam_idx] & (RENDERER_OAM_PRIORITY_MASK << RENDERER_OAM_PRIORITY_POS)) >> RENDERER_OAM_PRIORITY_POS) != 0U;
    }
    return false;
}

void rendererOamSetPriority(const uint8_t oam_idx, const bool is_priority)
{
    if (oam_idx < RENDERER_OAM_SIZE && (is_priority != rendererOamGetPriority(oam_idx)))
    {
        if (is_priority)
        {
            s_oam[oam_idx] |= (RENDERER_OAM_PRIORITY_MASK << RENDERER_OAM_PRIORITY_POS);
        }
        else
        {
            s_oam[oam_idx] &= ~(RENDERER_OAM_PRIORITY_MASK << RENDERER_OAM_PRIORITY_POS);
        }
        rendererOamSetIsDirty(oam_idx, true);
    }
}

bool rendererOamGetIsDirty(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return ((s_oam[oam_idx] & (RENDERER_OAM_IS_DIRTY_MASK << RENDERER_OAM_IS_DIRTY_POS)) >> RENDERER_OAM_IS_DIRTY_POS) != 0U;
    }
    return false;
}

void rendererOamSetIsDirty(const uint8_t oam_idx, const bool is_dirty)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        if (is_dirty)
        {
            s_oam[oam_idx] |= (RENDERER_OAM_IS_DIRTY_MASK << RENDERER_OAM_IS_DIRTY_POS);
        }
        else
        {
            s_oam[oam_idx] &= ~(RENDERER_OAM_IS_DIRTY_MASK << RENDERER_OAM_IS_DIRTY_POS);
        }
    }
}

uint8_t rendererOamGetPaletteIdx(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return (uint8_t)((s_oam[oam_idx] & (RENDERER_OAM_PALETTE_IDX_MASK << RENDERER_OAM_PALETTE_IDX_POS)) >> RENDERER_OAM_PALETTE_IDX_POS);
    }
    return 0U;
}

void rendererOamSetPaletteIdx(const uint8_t oam_idx, const uint8_t palette_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE && palette_idx < RENDERER_FRAME_PALETTE_SIZE && (palette_idx != rendererOamGetPaletteIdx(oam_idx)))
    {
        s_oam[oam_idx] &= ~(RENDERER_OAM_PALETTE_IDX_MASK << RENDERER_OAM_PALETTE_IDX_POS);
        s_oam[oam_idx] |= ((palette_idx & RENDERER_OAM_PALETTE_IDX_MASK) << RENDERER_OAM_PALETTE_IDX_POS);
        rendererOamSetIsDirty(oam_idx, true);
    }
}

uint8_t rendererOamGetTileIdx(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return (uint8_t)((s_oam[oam_idx] & (RENDERER_OAM_TILE_IDX_MASK << RENDERER_OAM_TILE_IDX_POS)) >> RENDERER_OAM_TILE_IDX_POS);
    }
    return 0U;
}

void rendererOamSetTileIdx(const uint8_t oam_idx, const uint8_t tile_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE && (tile_idx != rendererOamGetTileIdx(oam_idx)))
    {
        s_oam[oam_idx] &= ~(RENDERER_OAM_TILE_IDX_MASK << RENDERER_OAM_TILE_IDX_POS);
        s_oam[oam_idx] |= ((tile_idx & RENDERER_OAM_TILE_IDX_MASK) << RENDERER_OAM_TILE_IDX_POS);
        rendererOamSetIsDirty(oam_idx, true);
    }
}

uint8_t rendererOamGetYPos(const uint8_t oam_idx)
{
    if (oam_idx < RENDERER_OAM_SIZE)
    {
        return (uint8_t)((s_oam[oam_idx] & (RENDERER_OAM_Y_MASK << RENDERER_OAM_Y_POS)) >> RENDERER_OAM_Y_POS);
    }
    return 0U;
}

void rendererAttributeTableSetPalette(const uint8_t tile_x, const uint8_t tile_y, const uint8_t palette)
{
    rendererAttributeTableSetPaletteIdx(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y), palette);
}
uint8_t rendererAttributeTableGetPalette(const uint8_t tile_x, const uint8_t tile_y)
{
    return rendererAttributeTableGetPaletteIdx(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y));
}

void rendererAttributeTableSetFlipV(const uint8_t tile_x, const uint8_t tile_y, bool isFlipV)
{
    rendererAttributeTableSetFlipVIdx(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y), isFlipV);
}
bool rendererAttributeTableGetFlipV(const uint8_t tile_x, const uint8_t tile_y)
{
    return rendererAttributeTableGetFlipVIdx(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y));
}
void rendererAttributeTableSetFlipH(const uint8_t tile_x, const uint8_t tile_y, bool isFlipH)
{
    rendererAttributeTableSetFlipHIdx(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y), isFlipH);
}
bool rendererAttributeTableGetFlipH(const uint8_t tile_x, const uint8_t tile_y)
{
    return rendererAttributeTableGetFlipHIdx(RENDERER_HELPER_TILE_COORD_TO_INDEX(tile_x, tile_y));
}

uint16_t rendererGetSizeWidth()
{
    return RENDERER_WIDTH;
}

uint16_t rendererGetSizeHeight()
{
    return RENDERER_HEIGHT;
}

uint16_t rendererGetSizeTileScreen()
{
    return RENDERER_TILE_SCREEN_SIZE;
}

uint16_t rendererGetSizeTileMemory()
{
    return RENDERER_TILE_MEMORY_SIZE;
}

uint16_t rendererGetSizeFramePalette()
{
    return RENDERER_FRAME_PALETTE_SIZE;
}

uint16_t rendererGetSizePatternTable()
{
    return RENDERER_PATTERN_TABLE_SIZE;
}

uint16_t rendererGetSizeNameTable()
{
    return RENDERER_NAME_TABLE_SIZE;
}

uint16_t rendererGetSizeOam()
{
    return RENDERER_OAM_SIZE;
}

uint16_t rendererGetMaxTilesInRow()
{
    return RENDERER_TILES_IN_ROW;
}

uint16_t rendererGetMaxTilesInColumn()
{
    return RENDERER_TILES_IN_COLUMN;
}
