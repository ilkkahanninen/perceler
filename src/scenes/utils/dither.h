#ifndef DITHER_H
#define DITHER_H

/*
 * Ordered dithering threshold maps (8x8).
 *
 * Each matrix is 64 entries with values 0-63. To dither a pixel:
 *   threshold = map[(y & 7) * 8 + (x & 7)];
 *   output    = (value > threshold * 4) ? 1 : 0;   // for 1-bit
 * Or scale the threshold to match your quantization levels.
 */

/* Bayer 8x8 - recursive halftone pattern, uniform frequency response */
static const unsigned char dither_bayer8x8[64] = {
    0,  32, 8,  40, 2,  34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4,  36, 14, 46, 6,  38, 60, 28, 52, 20, 62, 30, 54, 22,
    3,  35, 11, 43, 1,  33, 9,  41, 51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7,  39, 13, 45, 5,  37, 63, 31, 55, 23, 61, 29, 53, 21};

/* Cluster dot 8x8 - groups lit pixels into round dots, mimics halftone printing
 */
static const unsigned char dither_cluster8x8[64] = {
    24, 10, 12, 26, 35, 47, 49, 37, 8,  0,  2,  14, 45, 59, 61, 51,
    22, 6,  4,  16, 43, 57, 63, 53, 30, 20, 18, 28, 33, 41, 55, 39,
    34, 46, 48, 36, 25, 11, 13, 27, 44, 58, 60, 50, 9,  1,  3,  15,
    42, 56, 62, 52, 23, 7,  5,  17, 32, 40, 54, 38, 31, 21, 19, 29};

/* Void-and-cluster 8x8 - blue-noise-like pattern, least visible artifacts */
static const unsigned char dither_voidcluster8x8[64] = {
    13, 39, 8,  52, 22, 56, 32, 44, 50, 24, 58, 28, 4,  36, 17, 10,
    30, 2,  46, 16, 48, 14, 62, 42, 54, 38, 20, 34, 60, 26, 6,  23,
    9,  63, 11, 55, 0,  40, 51, 35, 45, 18, 41, 25, 43, 19, 15, 57,
    27, 5,  53, 12, 31, 61, 3,  37, 33, 59, 21, 47, 7,  49, 29, 46};

#endif
