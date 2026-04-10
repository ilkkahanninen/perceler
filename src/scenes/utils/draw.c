#include "draw.h"

#include <vga.h>

void draw_line(unsigned char *buf, int x0, int y0, int x1, int y1,
               unsigned char color)
{
  int dx = x1 - x0;
  int dy = y1 - y0;
  int sx = (dx > 0) ? 1 : -1;
  int sy = (dy > 0) ? 1 : -1;
  int adx = dx * sx;
  int ady = dy * sy;
  int err;

  if (adx >= ady)
  {
    err = adx >> 1;
    while (x0 != x1)
    {
      if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
        buf[y0 * VGA_WIDTH + x0] = color;
      err -= ady;
      if (err < 0)
      {
        y0 += sy;
        err += adx;
      }
      x0 += sx;
    }
  }
  else
  {
    err = ady >> 1;
    while (y0 != y1)
    {
      if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
        buf[y0 * VGA_WIDTH + x0] = color;
      err -= adx;
      if (err < 0)
      {
        x0 += sx;
        err += ady;
      }
      y0 += sy;
    }
  }
  if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
    buf[y0 * VGA_WIDTH + x0] = color;
}

/* Flat-bottom or flat-top half-triangle scanline fill */
static void fill_scanlines(unsigned char *buf, int y_start, int y_end,
                           int x_left, int x_right, int dx_left, int dx_right,
                           unsigned char color)
{
  int y;
  for (y = y_start; y < y_end; y++)
  {
    if (y >= 0 && y < VGA_HEIGHT)
    {
      int lx = x_left >> 8;
      int rx = x_right >> 8;
      int sx, ex;
      if (lx > rx)
      {
        sx = rx;
        ex = lx;
      }
      else
      {
        sx = lx;
        ex = rx;
      }
      if (sx < 0)
        sx = 0;
      if (ex >= VGA_WIDTH)
        ex = VGA_WIDTH - 1;
      if (sx <= ex)
      {
        unsigned char *row = buf + y * VGA_WIDTH;
        int x;
        for (x = sx; x <= ex; x++)
          row[x] = color;
      }
    }
    x_left += dx_left;
    x_right += dx_right;
  }
}

void draw_triangle(unsigned char *buf, int x0, int y0, int x1, int y1,
                   int x2, int y2, unsigned char color)
{
  int tmp;

  /* Sort vertices by Y: y0 <= y1 <= y2 */
  if (y0 > y1)
  {
    tmp = y0; y0 = y1; y1 = tmp;
    tmp = x0; x0 = x1; x1 = tmp;
  }
  if (y1 > y2)
  {
    tmp = y1; y1 = y2; y2 = tmp;
    tmp = x1; x1 = x2; x2 = tmp;
  }
  if (y0 > y1)
  {
    tmp = y0; y0 = y1; y1 = tmp;
    tmp = x0; x0 = x1; x1 = tmp;
  }

  if (y0 == y2)
    return; /* Degenerate */

  {
    /* Edge slopes in 8.8 fixed-point (dx per scanline) */
    int dy_long = y2 - y0;
    int dx_long = ((x2 - x0) << 8) / dy_long;

    /* Upper half: y0 to y1 */
    if (y1 > y0)
    {
      int dy_upper = y1 - y0;
      int dx_upper = ((x1 - x0) << 8) / dy_upper;
      int xl = x0 << 8;
      int xr = xl;
      fill_scanlines(buf, y0, y1, xl, xr, dx_long, dx_upper, color);
    }

    /* Lower half: y1 to y2 */
    if (y2 > y1)
    {
      int dy_lower = y2 - y1;
      int dx_lower = ((x2 - x1) << 8) / dy_lower;
      /* Long edge X at y1 */
      int xl = (x0 << 8) + dx_long * (y1 - y0);
      int xr = x1 << 8;
      fill_scanlines(buf, y1, y2, xl, xr, dx_long, dx_lower, color);
    }
  }
}
