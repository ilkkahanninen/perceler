#ifndef PALETTE_H
#define PALETTE_H

/* 256-colour palette in VGA DAC range (0-63 per channel). */
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

/* Upload all 256 entries of `pal` to the VGA DAC. */
void palette_apply(const Palette *pal);

/* Read the live DAC contents back into `dst`. Handy for capturing the
 * outgoing palette at the top of init() — before applying your own —
 * so a scene can lerp between previous and current palettes during a
 * transition window. */
void palette_read(Palette *dst);

/*
 * Pre-calculate 64 lightness levels from `src`.
 * Levels 0-32 interpolate from black to the original palette.
 * Levels 32-63 interpolate from the original palette to white.
 */
void palette_calc_levels(PaletteLevels *dst, const Palette *src);

/*
 * Linear crossfade: dst = a + (b - a) * t / 256.
 * `t` is the Q?.8 fade factor: 0 → full a, 256 → full b. Values outside
 * [0, 256] are clamped. `dst` may alias either `a` or `b`.
 */
void palette_lerp(Palette *dst, const Palette *a, const Palette *b, int t);

/*
 * Fade `src` toward a solid VGA-range colour (0-63 per channel).
 * `t` is the Q?.8 fade factor: 0 → full src, 256 → solid (r, g, b).
 * Values outside [0, 256] are clamped. Useful for fade-to-black,
 * fade-to-white and arbitrary-colour flashes.
 */
void palette_fade(Palette *dst, const Palette *src,
                  unsigned char r, unsigned char g, unsigned char b, int t);

/*
 * Lookup table mapping (brightness_level, texel_index) -> palette index.
 *
 * Layout matches PaletteLevels: 64 levels where 0 = black, 32 =
 * original, 63 = white. Index as `map[(level << 8) | texel]`. Built
 * once per scene against the palette that is live on the DAC.
 */
typedef struct
{
  unsigned char map[64 * 256];
} Colormap;

/* Populate `dst` by darkening/brightening each entry of `pal` to each
 * of 64 levels and finding the nearest entry in `pal`. */
void colormap_build(Colormap *dst, const Palette *pal);

#endif
