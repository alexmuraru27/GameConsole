
#ifndef __TILE_CREATOR_H
#define __TILE_CREATOR_H
#include <stdint.h>

#define ENCODE_PLANE0(c0, c1, c2, c3, c4, c5, c6, c7)                                \
    ((((c0) & 1) << 7) | (((c1) & 1) << 6) | (((c2) & 1) << 5) | (((c3) & 1) << 4) | \
     (((c4) & 1) << 3) | (((c5) & 1) << 2) | (((c6) & 1) << 1) | (((c7) & 1) << 0))

#define ENCODE_PLANE1(c0, c1, c2, c3, c4, c5, c6, c7)                                                    \
    ((((c0) >> 1) & 1) << 7 | (((c1) >> 1) & 1) << 6 | (((c2) >> 1) & 1) << 5 | (((c3) >> 1) & 1) << 4 | \
     (((c4) >> 1) & 1) << 3 | (((c5) >> 1) & 1) << 2 | (((c6) >> 1) & 1) << 1 | (((c7) >> 1) & 1) << 0)

#define DEFINE_TILE(                                           \
    c00, c01, c02, c03, c04, c05, c06, c07,                    \
    c10, c11, c12, c13, c14, c15, c16, c17,                    \
    c20, c21, c22, c23, c24, c25, c26, c27,                    \
    c30, c31, c32, c33, c34, c35, c36, c37,                    \
    c40, c41, c42, c43, c44, c45, c46, c47,                    \
    c50, c51, c52, c53, c54, c55, c56, c57,                    \
    c60, c61, c62, c63, c64, c65, c66, c67,                    \
    c70, c71, c72, c73, c74, c75, c76, c77)                    \
    {                                                          \
        ENCODE_PLANE0(c00, c01, c02, c03, c04, c05, c06, c07), \
        ENCODE_PLANE0(c10, c11, c12, c13, c14, c15, c16, c17), \
        ENCODE_PLANE0(c20, c21, c22, c23, c24, c25, c26, c27), \
        ENCODE_PLANE0(c30, c31, c32, c33, c34, c35, c36, c37), \
        ENCODE_PLANE0(c40, c41, c42, c43, c44, c45, c46, c47), \
        ENCODE_PLANE0(c50, c51, c52, c53, c54, c55, c56, c57), \
        ENCODE_PLANE0(c60, c61, c62, c63, c64, c65, c66, c67), \
        ENCODE_PLANE0(c70, c71, c72, c73, c74, c75, c76, c77), \
        ENCODE_PLANE1(c00, c01, c02, c03, c04, c05, c06, c07), \
        ENCODE_PLANE1(c10, c11, c12, c13, c14, c15, c16, c17), \
        ENCODE_PLANE1(c20, c21, c22, c23, c24, c25, c26, c27), \
        ENCODE_PLANE1(c30, c31, c32, c33, c34, c35, c36, c37), \
        ENCODE_PLANE1(c40, c41, c42, c43, c44, c45, c46, c47), \
        ENCODE_PLANE1(c50, c51, c52, c53, c54, c55, c56, c57), \
        ENCODE_PLANE1(c60, c61, c62, c63, c64, c65, c66, c67), \
        ENCODE_PLANE1(c70, c71, c72, c73, c74, c75, c76, c77)}

#endif /* __TILE_CREATOR_H */