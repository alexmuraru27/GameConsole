
#ifndef __TILE_CREATOR_H
#define __TILE_CREATOR_H
#include <stdint.h>

#define ENCODE_PLANE0(c0, c1, c2, c3, c4, c5, c6, c7)                                \
    ((((c0) & 1) << 7) | (((c1) & 1) << 6) | (((c2) & 1) << 5) | (((c3) & 1) << 4) | \
     (((c4) & 1) << 3) | (((c5) & 1) << 2) | (((c6) & 1) << 1) | (((c7) & 1) << 0))

#define ENCODE_PLANE1(c0, c1, c2, c3, c4, c5, c6, c7)                                                    \
    ((((c0) >> 1) & 1) << 7 | (((c1) >> 1) & 1) << 6 | (((c2) >> 1) & 1) << 5 | (((c3) >> 1) & 1) << 4 | \
     (((c4) >> 1) & 1) << 3 | (((c5) >> 1) & 1) << 2 | (((c6) >> 1) & 1) << 1 | (((c7) >> 1) & 1) << 0)

// 64 Bytes
#define DEFINE_TILE_16(                                                                                                             \
    c00, c01, c02, c03, c04, c05, c06, c07, c08, c09, c0A, c0B, c0C, c0D, c0E, c0F,                                                 \
    c10, c11, c12, c13, c14, c15, c16, c17, c18, c19, c1A, c1B, c1C, c1D, c1E, c1F,                                                 \
    c20, c21, c22, c23, c24, c25, c26, c27, c28, c29, c2A, c2B, c2C, c2D, c2E, c2F,                                                 \
    c30, c31, c32, c33, c34, c35, c36, c37, c38, c39, c3A, c3B, c3C, c3D, c3E, c3F,                                                 \
    c40, c41, c42, c43, c44, c45, c46, c47, c48, c49, c4A, c4B, c4C, c4D, c4E, c4F,                                                 \
    c50, c51, c52, c53, c54, c55, c56, c57, c58, c59, c5A, c5B, c5C, c5D, c5E, c5F,                                                 \
    c60, c61, c62, c63, c64, c65, c66, c67, c68, c69, c6A, c6B, c6C, c6D, c6E, c6F,                                                 \
    c70, c71, c72, c73, c74, c75, c76, c77, c78, c79, c7A, c7B, c7C, c7D, c7E, c7F,                                                 \
    c80, c81, c82, c83, c84, c85, c86, c87, c88, c89, c8A, c8B, c8C, c8D, c8E, c8F,                                                 \
    c90, c91, c92, c93, c94, c95, c96, c97, c98, c99, c9A, c9B, c9C, c9D, c9E, c9F,                                                 \
    cA0, cA1, cA2, cA3, cA4, cA5, cA6, cA7, cA8, cA9, cAA, cAB, cAC, cAD, cAE, cAF,                                                 \
    cB0, cB1, cB2, cB3, cB4, cB5, cB6, cB7, cB8, cB9, cBA, cBB, cBC, cBD, cBE, cBF,                                                 \
    cC0, cC1, cC2, cC3, cC4, cC5, cC6, cC7, cC8, cC9, cCA, cCB, cCC, cCD, cCE, cCF,                                                 \
    cD0, cD1, cD2, cD3, cD4, cD5, cD6, cD7, cD8, cD9, cDA, cDB, cDC, cDD, cDE, cDF,                                                 \
    cE0, cE1, cE2, cE3, cE4, cE5, cE6, cE7, cE8, cE9, cEA, cEB, cEC, cED, cEE, cEF,                                                 \
    cF0, cF1, cF2, cF3, cF4, cF5, cF6, cF7, cF8, cF9, cFA, cFB, cFC, cFD, cFE, cFF)                                                 \
    {                                                                                                                               \
        /* Plane 0 */                                                                                                               \
        ENCODE_PLANE0(c00, c01, c02, c03, c04, c05, c06, c07), ENCODE_PLANE0(c08, c09, c0A, c0B, c0C, c0D, c0E, c0F),               \
        ENCODE_PLANE0(c10, c11, c12, c13, c14, c15, c16, c17), ENCODE_PLANE0(c18, c19, c1A, c1B, c1C, c1D, c1E, c1F),               \
        ENCODE_PLANE0(c20, c21, c22, c23, c24, c25, c26, c27), ENCODE_PLANE0(c28, c29, c2A, c2B, c2C, c2D, c2E, c2F),               \
        ENCODE_PLANE0(c30, c31, c32, c33, c34, c35, c36, c37), ENCODE_PLANE0(c38, c39, c3A, c3B, c3C, c3D, c3E, c3F),               \
        ENCODE_PLANE0(c40, c41, c42, c43, c44, c45, c46, c47), ENCODE_PLANE0(c48, c49, c4A, c4B, c4C, c4D, c4E, c4F),               \
        ENCODE_PLANE0(c50, c51, c52, c53, c54, c55, c56, c57), ENCODE_PLANE0(c58, c59, c5A, c5B, c5C, c5D, c5E, c5F),               \
        ENCODE_PLANE0(c60, c61, c62, c63, c64, c65, c66, c67), ENCODE_PLANE0(c68, c69, c6A, c6B, c6C, c6D, c6E, c6F),               \
        ENCODE_PLANE0(c70, c71, c72, c73, c74, c75, c76, c77), ENCODE_PLANE0(c78, c79, c7A, c7B, c7C, c7D, c7E, c7F),               \
        ENCODE_PLANE0(c80, c81, c82, c83, c84, c85, c86, c87), ENCODE_PLANE0(c88, c89, c8A, c8B, c8C, c8D, c8E, c8F),               \
        ENCODE_PLANE0(c90, c91, c92, c93, c94, c95, c96, c97), ENCODE_PLANE0(c98, c99, c9A, c9B, c9C, c9D, c9E, c9F),               \
        ENCODE_PLANE0(cA0, cA1, cA2, cA3, cA4, cA5, cA6, cA7), ENCODE_PLANE0(cA8, cA9, cAA, cAB, cAC, cAD, cAE, cAF),               \
        ENCODE_PLANE0(cB0, cB1, cB2, cB3, cB4, cB5, cB6, cB7), ENCODE_PLANE0(cB8, cB9, cBA, cBB, cBC, cBD, cBE, cBF),               \
        ENCODE_PLANE0(cC0, cC1, cC2, cC3, cC4, cC5, cC6, cC7), ENCODE_PLANE0(cC8, cC9, cCA, cCB, cCC, cCD, cCE, cCF),               \
        ENCODE_PLANE0(cD0, cD1, cD2, cD3, cD4, cD5, cD6, cD7), ENCODE_PLANE0(cD8, cD9, cDA, cDB, cDC, cDD, cDE, cDF),               \
        ENCODE_PLANE0(cE0, cE1, cE2, cE3, cE4, cE5, cE6, cE7), ENCODE_PLANE0(cE8, cE9, cEA, cEB, cEC, cED, cEE, cEF),               \
        ENCODE_PLANE0(cF0, cF1, cF2, cF3, cF4, cF5, cF6, cF7), ENCODE_PLANE0(cF8, cF9, cFA, cFB, cFC, cFD, cFE, cFF), /* Plane 1 */ \
        ENCODE_PLANE1(c00, c01, c02, c03, c04, c05, c06, c07), ENCODE_PLANE1(c08, c09, c0A, c0B, c0C, c0D, c0E, c0F),               \
        ENCODE_PLANE1(c10, c11, c12, c13, c14, c15, c16, c17), ENCODE_PLANE1(c18, c19, c1A, c1B, c1C, c1D, c1E, c1F),               \
        ENCODE_PLANE1(c20, c21, c22, c23, c24, c25, c26, c27), ENCODE_PLANE1(c28, c29, c2A, c2B, c2C, c2D, c2E, c2F),               \
        ENCODE_PLANE1(c30, c31, c32, c33, c34, c35, c36, c37), ENCODE_PLANE1(c38, c39, c3A, c3B, c3C, c3D, c3E, c3F),               \
        ENCODE_PLANE1(c40, c41, c42, c43, c44, c45, c46, c47), ENCODE_PLANE1(c48, c49, c4A, c4B, c4C, c4D, c4E, c4F),               \
        ENCODE_PLANE1(c50, c51, c52, c53, c54, c55, c56, c57), ENCODE_PLANE1(c58, c59, c5A, c5B, c5C, c5D, c5E, c5F),               \
        ENCODE_PLANE1(c60, c61, c62, c63, c64, c65, c66, c67), ENCODE_PLANE1(c68, c69, c6A, c6B, c6C, c6D, c6E, c6F),               \
        ENCODE_PLANE1(c70, c71, c72, c73, c74, c75, c76, c77), ENCODE_PLANE1(c78, c79, c7A, c7B, c7C, c7D, c7E, c7F),               \
        ENCODE_PLANE1(c80, c81, c82, c83, c84, c85, c86, c87), ENCODE_PLANE1(c88, c89, c8A, c8B, c8C, c8D, c8E, c8F),               \
        ENCODE_PLANE1(c90, c91, c92, c93, c94, c95, c96, c97), ENCODE_PLANE1(c98, c99, c9A, c9B, c9C, c9D, c9E, c9F),               \
        ENCODE_PLANE1(cA0, cA1, cA2, cA3, cA4, cA5, cA6, cA7), ENCODE_PLANE1(cA8, cA9, cAA, cAB, cAC, cAD, cAE, cAF),               \
        ENCODE_PLANE1(cB0, cB1, cB2, cB3, cB4, cB5, cB6, cB7), ENCODE_PLANE1(cB8, cB9, cBA, cBB, cBC, cBD, cBE, cBF),               \
        ENCODE_PLANE1(cC0, cC1, cC2, cC3, cC4, cC5, cC6, cC7), ENCODE_PLANE1(cC8, cC9, cCA, cCB, cCC, cCD, cCE, cCF),               \
        ENCODE_PLANE1(cD0, cD1, cD2, cD3, cD4, cD5, cD6, cD7), ENCODE_PLANE1(cD8, cD9, cDA, cDB, cDC, cDD, cDE, cDF),               \
        ENCODE_PLANE1(cE0, cE1, cE2, cE3, cE4, cE5, cE6, cE7), ENCODE_PLANE1(cE8, cE9, cEA, cEB, cEC, cED, cEE, cEF),               \
        ENCODE_PLANE1(cF0, cF1, cF2, cF3, cF4, cF5, cF6, cF7), ENCODE_PLANE1(cF8, cF9, cFA, cFB, cFC, cFD, cFE, cFF)}

#endif /* __TILE_CREATOR_H */