#include "test_renderer.h"
#include "renderer.h"
#include "pacman_ghost.h"
#include "bricks1.h"
#include "bricks2.h"
#include "bricks3.h"
#include "bricks4.h"
#include "bricks5.h"
#include "fonts.h"

static void testRendererInitBricks()
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

static void testRendererInitFonts()
{
    rendererPatternTableSetTile(1U, font_a_data, sizeof(font_a_data));
    rendererPatternTableSetTile(2U, font_b_data, sizeof(font_b_data));
    rendererPatternTableSetTile(3U, font_c_data, sizeof(font_c_data));
    rendererPatternTableSetTile(4U, font_d_data, sizeof(font_d_data));
    rendererPatternTableSetTile(5U, font_e_data, sizeof(font_e_data));
    rendererPatternTableSetTile(6U, font_f_data, sizeof(font_f_data));
    rendererPatternTableSetTile(7U, font_g_data, sizeof(font_g_data));
    rendererPatternTableSetTile(8U, font_h_data, sizeof(font_h_data));
    rendererPatternTableSetTile(9U, font_i_data, sizeof(font_i_data));
    rendererPatternTableSetTile(10U, font_j_data, sizeof(font_j_data));
    rendererPatternTableSetTile(11U, font_k_data, sizeof(font_k_data));
    rendererPatternTableSetTile(12U, font_l_data, sizeof(font_l_data));
    rendererPatternTableSetTile(13U, font_m_data, sizeof(font_m_data));
    rendererPatternTableSetTile(14U, font_n_data, sizeof(font_n_data));
    rendererPatternTableSetTile(15U, font_o_data, sizeof(font_o_data));
    rendererPatternTableSetTile(16U, font_p_data, sizeof(font_p_data));
    rendererPatternTableSetTile(17U, font_q_data, sizeof(font_q_data));
    rendererPatternTableSetTile(18U, font_r_data, sizeof(font_r_data));
    rendererPatternTableSetTile(19U, font_s_data, sizeof(font_s_data));
    rendererPatternTableSetTile(20U, font_t_data, sizeof(font_t_data));
    rendererPatternTableSetTile(21U, font_u_data, sizeof(font_u_data));
    rendererPatternTableSetTile(22U, font_v_data, sizeof(font_v_data));
    rendererPatternTableSetTile(23U, font_w_data, sizeof(font_w_data));
    rendererPatternTableSetTile(24U, font_x_data, sizeof(font_x_data));
    rendererPatternTableSetTile(25U, font_y_data, sizeof(font_y_data));
    rendererPatternTableSetTile(26U, font_z_data, sizeof(font_z_data));
    rendererPatternTableSetTile(27U, font_0_data, sizeof(font_0_data));
    rendererPatternTableSetTile(28U, font_1_data, sizeof(font_1_data));
    rendererPatternTableSetTile(29U, font_2_data, sizeof(font_2_data));
    rendererPatternTableSetTile(30U, font_3_data, sizeof(font_3_data));
    rendererPatternTableSetTile(31U, font_4_data, sizeof(font_4_data));
    rendererPatternTableSetTile(32U, font_5_data, sizeof(font_5_data));
    rendererPatternTableSetTile(33U, font_6_data, sizeof(font_6_data));
    rendererPatternTableSetTile(34U, font_7_data, sizeof(font_7_data));
    rendererPatternTableSetTile(35U, font_8_data, sizeof(font_8_data));
    rendererPatternTableSetTile(36U, font_9_data, sizeof(font_9_data));

    const uint8_t font_pallete_idx = 8U;
    rendererFramePaletteSetBackgroundMultiple(font_pallete_idx, font_0_palette[1U], font_0_palette[2U], font_0_palette[3U]);

    rendererNameTableSetTile(1U, 1U, 1U);
    rendererNameTableSetTile(2U, 1U, 2U);
    rendererNameTableSetTile(3U, 1U, 3U);
    rendererNameTableSetTile(4U, 1U, 4U);
    rendererNameTableSetTile(5U, 1U, 5U);
    rendererNameTableSetTile(6U, 1U, 6U);
    rendererNameTableSetTile(7U, 1U, 7U);
    rendererNameTableSetTile(8U, 1U, 8U);
    rendererNameTableSetTile(9U, 1U, 9U);
    rendererNameTableSetTile(10U, 1U, 10U);
    rendererNameTableSetTile(11U, 1U, 11U);
    rendererNameTableSetTile(12U, 1U, 12U);
    rendererNameTableSetTile(13U, 1U, 13U);
    rendererNameTableSetTile(14U, 1U, 14U);
    rendererNameTableSetTile(15U, 1U, 15U);
    rendererNameTableSetTile(1U, 2U, 16U);
    rendererNameTableSetTile(2U, 2U, 17U);
    rendererNameTableSetTile(3U, 2U, 18U);
    rendererNameTableSetTile(4U, 2U, 19U);
    rendererNameTableSetTile(5U, 2U, 20U);
    rendererNameTableSetTile(6U, 2U, 21U);
    rendererNameTableSetTile(7U, 2U, 22U);
    rendererNameTableSetTile(8U, 2U, 23U);
    rendererNameTableSetTile(9U, 2U, 24U);
    rendererNameTableSetTile(10U, 2U, 25U);
    rendererNameTableSetTile(11U, 2U, 26U);
    rendererNameTableSetTile(12U, 2U, 27U);
    rendererNameTableSetTile(13U, 2U, 28U);
    rendererNameTableSetTile(14U, 2U, 29U);
    rendererNameTableSetTile(15U, 2U, 30U);
    rendererNameTableSetTile(1U, 3U, 31U);
    rendererNameTableSetTile(2U, 3U, 32U);
    rendererNameTableSetTile(3U, 3U, 33U);
    rendererNameTableSetTile(4U, 3U, 34U);
    rendererNameTableSetTile(5U, 3U, 35U);
    rendererNameTableSetTile(6U, 3U, 36U);

    rendererAttributeTableSetPalette(1U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(2U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(3U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(4U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(5U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(6U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(7U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(8U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(9U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(10U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(11U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(12U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(13U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(14U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(15U, 1U, font_pallete_idx);
    rendererAttributeTableSetPalette(1U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(2U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(3U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(4U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(5U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(6U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(7U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(8U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(9U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(10U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(11U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(12U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(13U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(14U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(15U, 2U, font_pallete_idx);
    rendererAttributeTableSetPalette(1U, 3U, font_pallete_idx);
    rendererAttributeTableSetPalette(2U, 3U, font_pallete_idx);
    rendererAttributeTableSetPalette(3U, 3U, font_pallete_idx);
    rendererAttributeTableSetPalette(4U, 3U, font_pallete_idx);
    rendererAttributeTableSetPalette(5U, 3U, font_pallete_idx);
    rendererAttributeTableSetPalette(6U, 3U, font_pallete_idx);
}
void testRendererInit(void)
{
    testRendererInitBricks();
    // testRendererInitFonts();
}
