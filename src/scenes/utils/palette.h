#ifndef PALETTE_H
#define PALETTE_H

/* 256-color palette, VGA DAC range (0-63 per channel) */
typedef struct
{
  unsigned char entries[256][3]; /* [index][0=R, 1=G, 2=B] */
} Palette;

/*
 * 64 versions of a palette at different lightness levels.
 * Level 0 is black, level 32 is the original palette, level 63 is white.
 */
typedef struct
{
  Palette levels[64];
} PaletteLevels;

/* Upload palette to the VGA DAC (all 256 entries). */
void palette_apply(const Palette *pal);

/*
 * Pre-calculate 64 lightness levels from a source palette.
 * Levels 0-32 interpolate from black to the original palette.
 * Levels 32-63 interpolate from the original palette to white.
 */
void palette_calc_levels(PaletteLevels *dst, const Palette *src);

/*
 * Linear crossfade between two palettes: dst = a + (b - a) * t / 256.
 * t is a fixed-point fade factor: 0 → full a, 256 → full b. Values outside
 * [0, 256] are clamped. `dst` may alias either `a` or `b`.
 */
void palette_lerp(Palette *dst, const Palette *a, const Palette *b, int t);

/*
 * Fade a palette toward a solid VGA-range color (0-63 per channel).
 * t is a fixed-point fade factor: 0 → full src, 256 → solid (r, g, b).
 * Common uses: fade to black (r=g=b=0), fade to white (r=g=b=63), flash
 * effect to arbitrary color.
 */
void palette_fade(Palette *dst, const Palette *src,
                  unsigned char r, unsigned char g, unsigned char b, int t);

/*
 * Colormap LUT: maps (intensity_level, texel_index) -> palette index.
 *
 * Built once per scene from the palette that's actually live on the
 * VGA DAC. Lets the texture rasterizer apply per-pixel shading via a
 * single byte lookup — no per-pixel multiplies, ideal for indexed
 * 8-bit rendering (the Doom/Quake "COLORMAP" trick).
 *
 * Layout matches PaletteLevels: 64 levels where 0 = black, 32 =
 * original, 63 = white. Index lookup is `map[(level << 8) | texel]`.
 */
typedef struct
{
  unsigned char map[64 * 256];
} Colormap;

/* Build a colormap by darkening/brightening each palette entry to each
 * of 64 levels and finding the nearest entry in the same palette. */
void colormap_build(Colormap *dst, const Palette *pal);

#endif
