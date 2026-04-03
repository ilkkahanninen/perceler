#ifndef PALETTE_H
#define PALETTE_H

/* 256-color palette, VGA DAC range (0-63 per channel) */
typedef struct {
    unsigned char entries[256][3]; /* [index][0=R, 1=G, 2=B] */
} Palette;

/*
 * 64 versions of a palette at different lightness levels.
 * Level 0 is black, level 32 is the original palette, level 63 is white.
 */
typedef struct {
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

#endif
