#ifndef BITMAP_H
#define BITMAP_H

#include "../../assets.h"
#include "palette.h"

/*
 * 8-bit indexed BMP loader.
 *
 * Pixels are stored top-to-bottom, left-to-right, one byte per pixel.
 * Palette entries are in VGA DAC range (0-63 per channel). Index 0 is
 * treated as transparent by the blitters.
 */
typedef struct
{
  int width;
  int height;
  unsigned char *pixels; /* width * height bytes */
  Palette palette;
} Bitmap;

/* Load an 8-bit uncompressed BMP from the packed data file. Returns 0
 * on error. */
Bitmap *bitmap_load(Asset asset);

/* Free a bitmap returned by bitmap_load(). Safe to pass NULL. */
void bitmap_free(Bitmap *bmp);

/* Blit `bmp` directly to VGA at (dx, dy). Index-0 pixels are skipped.
 * Clipped to screen bounds. */
void bitmap_blit(const Bitmap *bmp, int dx, int dy);

/* Blit `bmp` onto a linear `dst` buffer of dimensions dst_w × dst_h at
 * (dx, dy). Index-0 pixels are skipped. Clipped to buffer bounds. */
void bitmap_blit_to_buffer(const Bitmap *bmp, unsigned char *dst, int dst_w,
                           int dst_h, int dx, int dy);

#endif
