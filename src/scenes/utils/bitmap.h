#ifndef BITMAP_H
#define BITMAP_H

#include "../../assets.h"
#include "palette.h"

/*
 * 8-bit indexed BMP loader.
 *
 * Pixels are stored top-to-bottom, left-to-right, one byte per pixel.
 * Palette entries are in VGA DAC range (0-63).
 *
 * Usage:
 *   Bitmap *logo = bitmap_load(ASSET_LOGO_BMP);
 *   palette_apply(&logo->palette);
 *   bitmap_blit(logo, x, y);            // direct to VGA, index 0 transparent
 *   bitmap_blit_to_buffer(logo, buf, w, h, x, y);  // to a backbuffer
 *   bitmap_free(logo);
 */

typedef struct
{
  int width;
  int height;
  unsigned char *pixels; /* width * height bytes */
  Palette palette;
} Bitmap;

/* Load an 8-bit uncompressed BMP from the packed data file. Returns 0 on error.
 */
Bitmap *bitmap_load(Asset asset);

/*
 * Blit bitmap to VGA at screen position (dx, dy).
 * Color index 0 is treated as transparent (not written).
 * Clips to screen bounds automatically.
 */
void bitmap_blit(const Bitmap *bmp, int dx, int dy);

/*
 * Blit bitmap onto a linear buffer at position (dx, dy).
 * Color index 0 is treated as transparent (not written).
 * Clips to dst_w x dst_h bounds.
 */
void bitmap_blit_to_buffer(const Bitmap *bmp, unsigned char *dst, int dst_w,
                           int dst_h, int dx, int dy);

/* Free a bitmap returned by bitmap_load(). */
void bitmap_free(Bitmap *bmp);

#endif
