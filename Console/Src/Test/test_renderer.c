#include "test_renderer.h"
#include "renderer.h"
#include "pacman_ghost.h"
#include "bricks1.h"
#include "bricks2.h"
#include "bricks3.h"
#include "bricks4.h"
#include "bricks5.h"

void testRendererInit(void)
{
    rendererPatternTableSetTile(1U, pacman_ghost_data, sizeof(pacman_ghost_data));
    rendererFramePaletteSetSpriteMultiple(0U, pacman_ghost_palette[1U], pacman_ghost_palette[2U], pacman_ghost_palette[3U]);
    rendererOamSetTileIdx(0U, 1U);
    rendererOamSetPaletteIdx(0U, 0U);
    rendererOamSetFlipH(0U, true);
    rendererOamSetPriorityLow(0U, true);

    // backgrounds
    rendererPatternTableSetTile(2U, bricks1_data, sizeof(bricks1_data));
    rendererPatternTableSetTile(3U, bricks2_data, sizeof(bricks2_data));
    rendererPatternTableSetTile(4U, bricks3_data, sizeof(bricks3_data));
    rendererPatternTableSetTile(5U, bricks4_data, sizeof(bricks4_data));
    rendererPatternTableSetTile(6U, bricks5_data, sizeof(bricks5_data));

    // background frame palette
    const uint8_t brick_pallete_idx = 1U;
    rendererFramePaletteSetBackgroundMultiple(brick_pallete_idx, bricks1_palette[1U], bricks1_palette[2U], bricks1_palette[3U]);

    // tile [2,0] flipped in both directions
    rendererAttributeTableSetFlipH(2U, 0U, true);
    rendererAttributeTableSetFlipV(2U, 0U, true);

    // background set nametable
    rendererNameTableSetTile(5U, 5U, 2U);
    rendererNameTableSetTile(5U, 5U + 1U, 3U);
    rendererNameTableSetTile(5U, 5U + 2U, 4U);
    rendererNameTableSetTile(5U, 5U + 3U, 5U);
    rendererNameTableSetTile(5U, 5U + 4U, 6U);

    rendererAttributeTableSetPalette(5U, 5U, brick_pallete_idx);
    rendererAttributeTableSetPalette(5U, 5U + 1U, brick_pallete_idx);
    rendererAttributeTableSetPalette(5U, 5U + 2U, brick_pallete_idx);
    rendererAttributeTableSetPalette(5U, 5U + 3U, brick_pallete_idx);
    rendererAttributeTableSetPalette(5U, 5U + 4U, brick_pallete_idx);

    for (uint8_t i = 0; i < rendererGetHeightTiles(); i++)
    {
        rendererNameTableSetTile(0U, i, 4U);
        rendererNameTableSetTile(rendererGetWidthTiles() - 1, i, 4U);

        rendererAttributeTableSetPalette(0U, i, brick_pallete_idx);
        rendererAttributeTableSetPalette(rendererGetWidthTiles() - 1, i, brick_pallete_idx);

        rendererAttributeTableSetPriorityHigh(0U, i, true);
        rendererAttributeTableSetPriorityHigh(rendererGetWidthTiles() - 1, i, true);
    }
    for (uint8_t i = 0; i < rendererGetWidthTiles(); i++)
    {
        rendererNameTableSetTile(i, 0U, 6U);
        rendererNameTableSetTile(i, rendererGetHeightTiles() - 1, 6U);

        rendererAttributeTableSetPalette(i, 0U, brick_pallete_idx);
        rendererAttributeTableSetPalette(i, rendererGetHeightTiles() - 1, brick_pallete_idx);

        rendererAttributeTableSetPriorityHigh(i, 0U, false);
        rendererAttributeTableSetPriorityHigh(i, rendererGetHeightTiles() - 1, true);
    }
}
