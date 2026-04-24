#include "palette.h"

#include <conio.h>

#define VGA_DAC_WRITE 0x3C8
#define VGA_DAC_DATA 0x3C9

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
  for (level = 0; level <= 32; level++)
  {
    for (i = 0; i < 256; i++)
    {
      for (ch = 0; ch < 3; ch++)
      {
        dst->levels[level].entries[i][ch] =
            (unsigned char)(src->entries[i][ch] * level / 32);
      }
    }
  }

  /* Levels 33-63: interpolate from original to white (63) */
  for (level = 33; level < 64; level++)
  {
    int t = level - 32; /* 1..31 */
    for (i = 0; i < 256; i++)
    {
      for (ch = 0; ch < 3; ch++)
      {
        unsigned char c = src->entries[i][ch];
        dst->levels[level].entries[i][ch] =
            (unsigned char)(c + (63 - c) * t / 31);
      }
    }
  }
}

void palette_lerp(Palette *dst, const Palette *a, const Palette *b, int t)
{
  const unsigned char *pa = &a->entries[0][0];
  const unsigned char *pb = &b->entries[0][0];
  unsigned char *pd = &dst->entries[0][0];
  int inv;
  int i;

  if (t < 0)
    t = 0;
  else if (t > 256)
    t = 256;
  inv = 256 - t;

  /* Unsigned mix: pd = (pa * inv + pb * t) >> 8.
   * Both inputs in [0, 63] and inv+t = 256, so result stays in range. */
  for (i = 0; i < 256 * 3; i++)
    pd[i] = (unsigned char)((pa[i] * inv + pb[i] * t) >> 8);
}

void palette_fade(Palette *dst, const Palette *src,
                  unsigned char r, unsigned char g, unsigned char b, int t)
{
  const unsigned char *ps = &src->entries[0][0];
  unsigned char *pd = &dst->entries[0][0];
  int inv;
  int rt, gt, bt;
  int i;

  if (t < 0)
    t = 0;
  else if (t > 256)
    t = 256;
  inv = 256 - t;
  rt = r * t;
  gt = g * t;
  bt = b * t;

  for (i = 0; i < 256; i++)
  {
    pd[0] = (unsigned char)((ps[0] * inv + rt) >> 8);
    pd[1] = (unsigned char)((ps[1] * inv + gt) >> 8);
    pd[2] = (unsigned char)((ps[2] * inv + bt) >> 8);
    ps += 3;
    pd += 3;
  }
}
