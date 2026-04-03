#include <conio.h>
#include "palette.h"

#define VGA_DAC_WRITE 0x3C8
#define VGA_DAC_DATA  0x3C9

void palette_apply(const Palette *pal)
{
    const unsigned char *p = &pal->entries[0][0];
    int i;

    outp(VGA_DAC_WRITE, 0);
    for (i = 0; i < 256 * 3; i++)
        outp(VGA_DAC_DATA, p[i]);
}

void palette_calc_levels(PaletteLevels *dst, const Palette *src)
{
    int level, i, ch;

    /* Levels 0-32: interpolate from black to original */
    for (level = 0; level <= 32; level++) {
        for (i = 0; i < 256; i++) {
            for (ch = 0; ch < 3; ch++) {
                dst->levels[level].entries[i][ch] =
                    (unsigned char)(src->entries[i][ch] * level / 32);
            }
        }
    }

    /* Levels 33-63: interpolate from original to white (63) */
    for (level = 33; level < 64; level++) {
        int t = level - 32; /* 1..31 */
        for (i = 0; i < 256; i++) {
            for (ch = 0; ch < 3; ch++) {
                unsigned char c = src->entries[i][ch];
                dst->levels[level].entries[i][ch] =
                    (unsigned char)(c + (63 - c) * t / 31);
            }
        }
    }
}
