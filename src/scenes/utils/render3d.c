#include "render3d.h"

#include "math.h"
#include <vga.h>

/* recip_tab[i] = (1 << 16) / i, for i in 1..127.
 * Replaces IDIV with IMUL+SAR for small divisors — the common case for
 * triangle edge slopes on a small-on-screen model. Built lazily on the
 * first call into the rasterizer. */
static int recip_tab[128];
static int recip_tab_ready;

static void init_recip_tab(void)
{
  int i;
  recip_tab[0] = 0;
  for (i = 1; i < 128; i++)
    recip_tab[i] = (1 << 16) / i;
  recip_tab_ready = 1;
}

void transform_points(int *dst, const int *src, int count,
                      unsigned char angle_y, unsigned char angle_x,
                      int translate_z)
{
  int sin_y = sin8(angle_y), cos_y = cos8(angle_y);
  int sin_x = sin8(angle_x), cos_x = cos8(angle_x);
  int i;
  for (i = 0; i < count; i++, src += 3, dst += 3)
  {
    int x = src[0], y = src[1], z = src[2];
    int rx = FP_MUL(x, cos_y) + FP_MUL(z, sin_y);
    int ry = y;
    int rz = FP_MUL(-x, sin_y) + FP_MUL(z, cos_y);
    dst[0] = rx;
    dst[1] = FP_MUL(ry, cos_x) - FP_MUL(rz, sin_x);
    dst[2] = FP_MUL(ry, sin_x) + FP_MUL(rz, cos_x) + translate_z;
  }
}

void transform_dirs(int *dst, const int *src, int count,
                    unsigned char angle_y, unsigned char angle_x)
{
  int sin_y = sin8(angle_y), cos_y = cos8(angle_y);
  int sin_x = sin8(angle_x), cos_x = cos8(angle_x);
  int i;
  for (i = 0; i < count; i++, src += 3, dst += 3)
  {
    int nx = src[0], ny = src[1], nz = src[2];
    int rx = FP_MUL(nx, cos_y) + FP_MUL(nz, sin_y);
    int ry = ny;
    int rz = FP_MUL(-nx, sin_y) + FP_MUL(nz, cos_y);
    dst[0] = rx;
    dst[1] = FP_MUL(ry, cos_x) - FP_MUL(rz, sin_x);
    dst[2] = FP_MUL(ry, sin_x) + FP_MUL(rz, cos_x);
  }
}

int project3d(const Camera3D *cam, int x, int y, int z, int *sx, int *sy)
{
  int s;
  if (z < cam->near_z)
    return 0;
  /* Compute proj_scale/z once, then multiply — 1 IDIV + 2 MULs instead
   * of 2 IDIVs + 2 MULs per vertex. */
  s = FP_DIV(cam->proj_scale, z);
  *sx = cam->cx + FP_TO_INT(FP_MUL(x, s));
  *sy = cam->cy - FP_TO_INT(FP_MUL(y, s));
  return 1;
}

int backface3d(const int *n, const int *v)
{
  return FP_MUL(n[0], v[0]) + FP_MUL(n[1], v[1]) + FP_MUL(n[2], v[2]) > 0;
}

/* (num << 8) / dy. Uses recip_tab for dy < 128 (common case) to turn
 * IDIV (~46 cycles) into IMUL + SAR (~12 cycles). */
#define DIV_SHIFTED(num, dy)                                   \
  (((unsigned)(dy) < 128u) ? ((int)(num) * recip_tab[dy] >> 8) \
                           : (((int)(num) << 8) / (int)(dy)))

/* num / dy, same idea. */
#define DIV_PLAIN(num, dy)                                      \
  (((unsigned)(dy) < 128u) ? ((int)(num) * recip_tab[dy] >> 16) \
                           : ((int)(num) / (int)(dy)))

void fill_triangle_flat(unsigned char *buf, unsigned short *zb,
                        int x0, int y0, int z0,
                        int x1, int y1, int z1,
                        int x2, int y2, int z2,
                        unsigned char color)
{
  int iz0, iz1, iz2;
  int dy_long, dx_long, diz_long;
  int y, half;

  if (!recip_tab_ready)
    init_recip_tab();

  /* Sort by Y ascending */
  if (y0 > y1)
  {
    SWAP(x0, x1);
    SWAP(y0, y1);
    SWAP(z0, z1);
  }
  if (y1 > y2)
  {
    SWAP(x1, x2);
    SWAP(y1, y2);
    SWAP(z1, z2);
  }
  if (y0 > y1)
  {
    SWAP(x0, x1);
    SWAP(y0, y1);
    SWAP(z0, z1);
  }

  /* Early-out for 1-pixel triangles (all three corners on the same pixel).
   * Common at distance; skips all edge-setup IDIVs and inner loops. */
  if (y0 == y2 && x0 == x1 && x1 == x2)
  {
    if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
    {
      int iz = (z0 > 0) ? (int)((1L << 20) / z0) : 0xFFFF;
      int idx = y0 * VGA_WIDTH + x0;
      if (iz > zb[idx])
      {
        zb[idx] = (unsigned short)iz;
        buf[idx] = color;
      }
    }
    return;
  }

  if (y0 == y2 || y2 < 0 || y0 >= VGA_HEIGHT)
    return;

  iz0 = (z0 > 0) ? (int)((1L << 20) / z0) : 0xFFFF;
  iz1 = (z1 > 0) ? (int)((1L << 20) / z1) : 0xFFFF;
  iz2 = (z2 > 0) ? (int)((1L << 20) / z2) : 0xFFFF;

  dy_long = y2 - y0;
  dx_long = DIV_SHIFTED(x2 - x0, dy_long);
  diz_long = DIV_SHIFTED(iz2 - iz0, dy_long);

  for (half = 0; half < 2; half++)
  {
    int ya, yb, dx_short, diz_short;
    int xl, xr, izl, izr;

    if (half == 0)
    {
      int dy = y1 - y0;
      ya = y0;
      yb = y1;
      dx_short = dy ? DIV_SHIFTED(x1 - x0, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz1 - iz0, dy) : 0;
      xl = x0 << 8;
      xr = xl;
      izl = iz0 << 8;
      izr = izl;
    }
    else
    {
      int dy = y2 - y1;
      ya = y1;
      yb = y2;
      dx_short = dy ? DIV_SHIFTED(x2 - x1, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz2 - iz1, dy) : 0;
      xl = (x0 << 8) + dx_long * (y1 - y0);
      xr = x1 << 8;
      izl = (iz0 << 8) + diz_long * (y1 - y0);
      izr = iz1 << 8;
    }

    for (y = ya; y < yb; y++)
    {
      if (y >= 0 && y < VGA_HEIGHT)
      {
        int lx = xl >> 8, rx = xr >> 8;
        int liz = izl >> 8, riz = izr >> 8;
        int sx, ex, off, x, iz, diz;

        if (lx > rx)
        {
          SWAP(lx, rx);
          SWAP(liz, riz);
        }

        sx = lx < 0 ? 0 : lx;
        ex = rx >= VGA_WIDTH ? VGA_WIDTH - 1 : rx;

        if (sx <= ex)
        {
          int span = rx - lx;
          off = y * VGA_WIDTH;
          diz = span ? DIV_PLAIN(riz - liz, span) : 0;
          iz = liz + diz * (sx - lx);

          for (x = sx; x <= ex; x++)
          {
            if (iz > zb[off + x])
            {
              zb[off + x] = (unsigned short)iz;
              buf[off + x] = color;
            }
            iz += diz;
          }
        }
      }
      xl += dx_long;
      xr += dx_short;
      izl += diz_long;
      izr += diz_short;
    }
  }
}

void fill_triangle_gouraud(unsigned char *buf, unsigned short *zb,
                           int x0, int y0, int z0, int i0,
                           int x1, int y1, int z1, int i1,
                           int x2, int y2, int z2, int i2)
{
  int iz0, iz1, iz2;
  int dy_long, dx_long, diz_long, di_long;
  int y, half;

  if (!recip_tab_ready)
    init_recip_tab();

  /* Sort by Y ascending; intensity travels with each vertex. */
  if (y0 > y1)
  {
    SWAP(x0, x1);
    SWAP(y0, y1);
    SWAP(z0, z1);
    SWAP(i0, i1);
  }
  if (y1 > y2)
  {
    SWAP(x1, x2);
    SWAP(y1, y2);
    SWAP(z1, z2);
    SWAP(i1, i2);
  }
  if (y0 > y1)
  {
    SWAP(x0, x1);
    SWAP(y0, y1);
    SWAP(z0, z1);
    SWAP(i0, i1);
  }

  /* 1-pixel early-out (degenerate triangle). */
  if (y0 == y2 && x0 == x1 && x1 == x2)
  {
    if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
    {
      int iz = (z0 > 0) ? (int)((1L << 20) / z0) : 0xFFFF;
      int idx = y0 * VGA_WIDTH + x0;
      if (iz > zb[idx])
      {
        int c = i0;
        if (c < 0) c = 0;
        else if (c > 255) c = 255;
        zb[idx] = (unsigned short)iz;
        buf[idx] = (unsigned char)c;
      }
    }
    return;
  }

  if (y0 == y2 || y2 < 0 || y0 >= VGA_HEIGHT)
    return;

  iz0 = (z0 > 0) ? (int)((1L << 20) / z0) : 0xFFFF;
  iz1 = (z1 > 0) ? (int)((1L << 20) / z1) : 0xFFFF;
  iz2 = (z2 > 0) ? (int)((1L << 20) / z2) : 0xFFFF;

  dy_long = y2 - y0;
  dx_long = DIV_SHIFTED(x2 - x0, dy_long);
  diz_long = DIV_SHIFTED(iz2 - iz0, dy_long);
  di_long = DIV_SHIFTED(i2 - i0, dy_long);

  for (half = 0; half < 2; half++)
  {
    int ya, yb, dx_short, diz_short, di_short;
    int xl, xr, izl, izr, il, ir;

    if (half == 0)
    {
      int dy = y1 - y0;
      ya = y0;
      yb = y1;
      dx_short = dy ? DIV_SHIFTED(x1 - x0, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz1 - iz0, dy) : 0;
      di_short = dy ? DIV_SHIFTED(i1 - i0, dy) : 0;
      xl = x0 << 8;
      xr = xl;
      izl = iz0 << 8;
      izr = izl;
      il = i0 << 8;
      ir = il;
    }
    else
    {
      int dy = y2 - y1;
      ya = y1;
      yb = y2;
      dx_short = dy ? DIV_SHIFTED(x2 - x1, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz2 - iz1, dy) : 0;
      di_short = dy ? DIV_SHIFTED(i2 - i1, dy) : 0;
      xl = (x0 << 8) + dx_long * (y1 - y0);
      xr = x1 << 8;
      izl = (iz0 << 8) + diz_long * (y1 - y0);
      izr = iz1 << 8;
      il = (i0 << 8) + di_long * (y1 - y0);
      ir = i1 << 8;
    }

    for (y = ya; y < yb; y++)
    {
      if (y >= 0 && y < VGA_HEIGHT)
      {
        int lx = xl >> 8, rx = xr >> 8;
        int liz = izl >> 8, riz = izr >> 8;
        /* Keep intensity in Q8.8 across the span — collapsing to plain
         * int here makes neighbouring triangles' rounded slopes diverge
         * along their shared edge and produces visible seams. */
        int li88 = il, ri88 = ir;
        int sx, ex, off, x, iz, diz, ii88, dii88;

        if (lx > rx)
        {
          SWAP(lx, rx);
          SWAP(liz, riz);
          SWAP(li88, ri88);
        }

        sx = lx < 0 ? 0 : lx;
        ex = rx >= VGA_WIDTH ? VGA_WIDTH - 1 : rx;

        if (sx <= ex)
        {
          int span = rx - lx;
          off = y * VGA_WIDTH;
          diz = span ? DIV_PLAIN(riz - liz, span) : 0;
          dii88 = span ? DIV_PLAIN(ri88 - li88, span) : 0;
          iz = liz + diz * (sx - lx);
          ii88 = li88 + dii88 * (sx - lx);

          for (x = sx; x <= ex; x++)
          {
            if (iz > zb[off + x])
            {
              int c = ii88 >> 8;
              if (c < 0) c = 0;
              else if (c > 255) c = 255;
              zb[off + x] = (unsigned short)iz;
              buf[off + x] = (unsigned char)c;
            }
            iz += diz;
            ii88 += dii88;
          }
        }
      }
      xl += dx_long;
      xr += dx_short;
      izl += diz_long;
      izr += diz_short;
      il += di_long;
      ir += di_short;
    }
  }
}
