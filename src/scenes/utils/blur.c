#include "blur.h"

#include <vga.h>

static void hblur_row(const unsigned char *src, unsigned char *dst)
{
  const unsigned char *left = src;
  const unsigned char *mid = src + 1;
  const unsigned char *right = src + 2;
  int n = VGA_WIDTH - 2;
  int blocks = n >> 2;
  int tail = n & 3;

  dst[0] = (unsigned char)((src[0] * 3 + src[1]) >> 2);
  dst++;

  /* SWAR main loop: 4 pixels per iteration via 32-bit arithmetic.
   * Per-lane shifts with masks prevent bit leakage between bytes.
   * Max per-lane sum: 63 + 127 + 63 = 253 < 256, so no carries leak. */
  while (blocks--)
  {
    unsigned int a = *(const unsigned int *)left;
    unsigned int b = *(const unsigned int *)mid;
    unsigned int c = *(const unsigned int *)right;
    *(unsigned int *)dst = ((a >> 2) & 0x3F3F3F3FU) + ((b >> 1) & 0x7F7F7F7FU) + ((c >> 2) & 0x3F3F3F3FU);
    dst += 4;
    left += 4;
    mid += 4;
    right += 4;
  }
  while (tail--)
    *dst++ = (unsigned char)((*left++ + (*mid++ << 1) + *right++) >> 2);

  *dst = (unsigned char)((src[VGA_WIDTH - 2] + src[VGA_WIDTH - 1] * 3) >> 2);
}

void blur(unsigned char *buf)
{
  /* 3-row rolling buffer, ~960 bytes — stays in L1 */
  static unsigned char rows[3][VGA_WIDTH];
  unsigned char *prev = rows[0];
  unsigned char *curr = rows[1];
  unsigned char *next = rows[2];
  unsigned char *swap;
  unsigned char *dst;
  int x, y;

  /* Prime with hblurred rows 0 and 1 */
  hblur_row(buf, prev);
  hblur_row(buf + VGA_WIDTH, curr);

  /* First output row: no previous row above, weight curr by 3 */
  for (x = 0; x < VGA_WIDTH; x++)
    buf[x] = (unsigned char)((prev[x] * 3 + curr[x]) >> 2);

  /* Middle rows: hblur row y+1 into 'next', then combine vertically.
   * Safe in place: buf[y*w..] is consumed before being overwritten. */
  for (y = 1; y < VGA_HEIGHT - 1; y++)
  {
    unsigned char *p = prev;
    unsigned char *c = curr;
    unsigned char *nx = next;
    dst = buf + y * VGA_WIDTH;
    hblur_row(buf + (y + 1) * VGA_WIDTH, next);

    /* SWAR: 4 pixels per iteration; VGA_WIDTH=320 is a multiple of 4.
     * All three row pointers are aligned here (rows start on row boundaries). */
    for (x = VGA_WIDTH >> 2; x > 0; x--)
    {
      unsigned int pv = *(const unsigned int *)p;
      unsigned int cv = *(const unsigned int *)c;
      unsigned int nv = *(const unsigned int *)nx;
      *(unsigned int *)dst = ((pv >> 2) & 0x3F3F3F3FU) + ((cv >> 1) & 0x7F7F7F7FU) + ((nv >> 2) & 0x3F3F3F3FU);
      dst += 4;
      p += 4;
      c += 4;
      nx += 4;
    }

    swap = prev;
    prev = curr;
    curr = next;
    next = swap;
  }

  /* Last output row: no next row below, weight curr by 3 */
  dst = buf + (VGA_HEIGHT - 1) * VGA_WIDTH;
  for (x = 0; x < VGA_WIDTH; x++)
    dst[x] = (unsigned char)((prev[x] + curr[x] * 3) >> 2);
}
