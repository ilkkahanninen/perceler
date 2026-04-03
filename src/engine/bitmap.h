#ifndef BITMAP_H
#define BITMAP_H

#include "../assets.h"
#include "../utils/palette.h"

/*
 * 8-bit indexed BMP loader.
 *
 * Pixels are stored top-to-bottom, left-to-right, one byte per pixel.
 * Palette entries are in VGA DAC range (0-63).
 */

typedef struct {
    int           width;
    int           height;
    unsigned char *pixels;  /* width * height bytes */
    Palette       palette;
} Bitmap;

/* Load an 8-bit uncompressed BMP from the packed data file. Returns 0 on error. */
Bitmap *bitmap_load(Asset asset);

/*
 * Blit bitmap to VGA page at screen position (dx, dy).
 * Color index 0 is treated as transparent (not written).
 * Clips to screen bounds automatically.
 */
void    bitmap_blit(const Bitmap *bmp, int dx, int dy, unsigned int page);

/* Free a bitmap returned by bitmap_load(). */
void    bitmap_free(Bitmap *bmp);

#endif
