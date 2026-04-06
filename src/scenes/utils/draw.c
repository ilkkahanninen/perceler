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
