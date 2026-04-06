#ifndef DITHER_H
#define DITHER_H

/*
 * Ordered dithering threshold maps (8x8).
 *
 * Each matrix is 64 entries with values pre-scaled to 0-252.
 * To dither a pixel:
 *   threshold = map[(y & 7) * 8 + (x & 7)];
 *   output    = (value > threshold) ? 1 : 0;
 */

/* Bayer 8x8 - recursive halftone pattern, uniform frequency response */
static const unsigned char dither_bayer8x8[64] = {
    0, 128, 32, 160, 8, 136, 40, 168, 192, 64, 224, 96, 200, 72, 232, 104,
    48, 176, 16, 144, 56, 184, 24, 152, 240, 112, 208, 80, 248, 120, 216, 88,
    12, 140, 44, 172, 4, 132, 36, 164, 204, 76, 236, 108, 196, 68, 228, 100,
    60, 188, 28, 156, 52, 180, 20, 148, 252, 124, 220, 92, 244, 116, 212, 84};

/* Cluster dot 8x8 - groups lit pixels into round dots, mimics halftone printing
 */
static const unsigned char dither_cluster8x8[64] = {
    96, 40, 48, 104, 140, 188, 196, 148, 32, 0, 8, 56, 180, 236, 244, 204,
    88, 24, 16, 64, 172, 228, 252, 212, 120, 80, 72, 112, 132, 164, 220, 156,
    136, 184, 192, 144, 100, 44, 52, 108, 176, 232, 240, 200, 36, 4, 12, 60,
    168, 224, 248, 208, 92, 28, 20, 68, 128, 160, 216, 152, 124, 84, 76, 116};

/* Void-and-cluster 8x8 - blue-noise-like pattern, least visible artifacts */
static const unsigned char dither_voidcluster8x8[64] = {
    52, 156, 32, 208, 88, 224, 128, 176, 200, 96, 232, 112, 16, 144, 68, 40,
    120, 8, 184, 64, 192, 56, 248, 168, 216, 152, 80, 136, 240, 104, 24, 92,
    36, 252, 44, 220, 0, 160, 204, 140, 180, 72, 164, 100, 172, 76, 60, 228,
    108, 20, 212, 48, 124, 244, 12, 148, 132, 236, 84, 188, 28, 196, 116, 184};

static inline int dither_threshold(const unsigned char *map, int x, int y,
                                   unsigned char value)
{
    return value > map[((y & 7) << 3) + (x & 7)];
}

#endif
