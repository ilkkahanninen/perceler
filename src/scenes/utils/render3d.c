#include "render3d.h"

#include "bitmap.h"
#include "math.h"
#include <stdlib.h>
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

Texture *texture_load(Asset asset)
{
  Bitmap *bmp;
  Texture *tex;
  int log2;

  bmp = bitmap_load(asset);
  if (!bmp)
    return 0;

  /* Square + power-of-two + at most 256. */
  if (bmp->width != bmp->height || bmp->width <= 0 ||
      bmp->width > 256 || (bmp->width & (bmp->width - 1)))
  {
    bitmap_free(bmp);
    return 0;
  }
  for (log2 = 0; (1 << log2) < bmp->width; log2++)
    ;

  tex = (Texture *)malloc(sizeof(Texture));
  if (!tex)
  {
    bitmap_free(bmp);
    return 0;
  }
  /* Steal the pixel buffer + palette out of the Bitmap so we don't
   * pay for the wrapper struct at runtime. */
  tex->pixels = bmp->pixels;
  tex->size = bmp->width;
  tex->log2_size = log2;
  tex->palette = bmp->palette;
  bmp->pixels = 0;
  bitmap_free(bmp);
  return tex;
}

void texture_free(Texture *tex)
{
  if (!tex)
    return;
  free(tex->pixels);
  free(tex);
}

/* Pixels per perspective-correct anchor in fill_triangle_textured.
 * Inside each chunk UVs are interpolated affinely (cheap), and the
 * endpoints are recomputed via uoz/voz/iz divides — large enough to
 * hide most affine swimming, small enough that the divides stay
 * cheap. Quake 1 used 16. */
#define TEX_SUBDIV 16

void fill_triangle_textured(unsigned char *buf, unsigned short *zb,
                            int x0, int y0, int z0, int u0, int v0,
                            int x1, int y1, int z1, int u1, int v1,
                            int x2, int y2, int z2, int u2, int v2,
                            const Texture *tex)
{
  int iz0, iz1, iz2;
  int uoz0, uoz1, uoz2, voz0, voz1, voz2;
  int dy_long, dx_long, diz_long, duoz_long, dvoz_long;
  int y, half;
  const unsigned char *texels = tex->pixels;
  int log2_size = tex->log2_size;
  int uv_mask = tex->size - 1;
  /* UVs are Q8.8 where 256 = one full wrap. The integer part is the
   * texel index for a 256-wide texture; for smaller textures drop
   * (8 - log2_size) low bits so 256 still maps to one full wrap.
   * Inside the inner loop we keep an extra 8 fractional bits (Q?.16),
   * so add 8 to the shift when sampling. */
  int uv_shift = 8 - log2_size;

  if (!recip_tab_ready)
    init_recip_tab();

  /* Sort by Y; UVs travel with each vertex. */
  if (y0 > y1)
  {
    SWAP(x0, x1); SWAP(y0, y1); SWAP(z0, z1);
    SWAP(u0, u1); SWAP(v0, v1);
  }
  if (y1 > y2)
  {
    SWAP(x1, x2); SWAP(y1, y2); SWAP(z1, z2);
    SWAP(u1, u2); SWAP(v1, v2);
  }
  if (y0 > y1)
  {
    SWAP(x0, x1); SWAP(y0, y1); SWAP(z0, z1);
    SWAP(u0, u1); SWAP(v0, v1);
  }

  iz0 = (z0 > 0) ? (int)((1L << 20) / z0) : 0xFFFF;
  iz1 = (z1 > 0) ? (int)((1L << 20) / z1) : 0xFFFF;
  iz2 = (z2 > 0) ? (int)((1L << 20) / z2) : 0xFFFF;

  /* uoz / voz = u * (1/z), v * (1/z). These three quantities (uoz, voz,
   * iz) are linear in screen space — that's the whole point of
   * perspective-correct mapping. */
  uoz0 = u0 * iz0; uoz1 = u1 * iz1; uoz2 = u2 * iz2;
  voz0 = v0 * iz0; voz1 = v1 * iz1; voz2 = v2 * iz2;

  /* 1-pixel early-out (degenerate). */
  if (y0 == y2 && x0 == x1 && x1 == x2)
  {
    if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
    {
      int idx = y0 * VGA_WIDTH + x0;
      if (iz0 > zb[idx])
      {
        int tu = (u0 >> uv_shift) & uv_mask;
        int tv = (v0 >> uv_shift) & uv_mask;
        zb[idx] = (unsigned short)iz0;
        buf[idx] = texels[(tv << log2_size) + tu];
      }
    }
    return;
  }

  if (y0 == y2 || y2 < 0 || y0 >= VGA_HEIGHT)
    return;

  dy_long = y2 - y0;
  dx_long = DIV_SHIFTED(x2 - x0, dy_long);
  diz_long = DIV_SHIFTED(iz2 - iz0, dy_long);
  /* uoz / voz live in a much larger range than x or iz, so plain-int
   * per-row slopes are fine without the Q?.8 boost — the cumulative
   * truncation error is dwarfed by the magnitudes involved, and the
   * per-chunk divide re-anchors UV anyway. */
  duoz_long = (uoz2 - uoz0) / dy_long;
  dvoz_long = (voz2 - voz0) / dy_long;

  for (half = 0; half < 2; half++)
  {
    int ya, yb, dx_short, diz_short, duoz_short, dvoz_short;
    int xl, xr, izl, izr, uozl, uozr, vozl, vozr;

    if (half == 0)
    {
      int dy = y1 - y0;
      ya = y0;
      yb = y1;
      dx_short = dy ? DIV_SHIFTED(x1 - x0, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz1 - iz0, dy) : 0;
      duoz_short = dy ? (uoz1 - uoz0) / dy : 0;
      dvoz_short = dy ? (voz1 - voz0) / dy : 0;
      xl = x0 << 8; xr = xl;
      izl = iz0 << 8; izr = izl;
      uozl = uoz0; uozr = uoz0;
      vozl = voz0; vozr = voz0;
    }
    else
    {
      int dy = y2 - y1;
      ya = y1;
      yb = y2;
      dx_short = dy ? DIV_SHIFTED(x2 - x1, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz2 - iz1, dy) : 0;
      duoz_short = dy ? (uoz2 - uoz1) / dy : 0;
      dvoz_short = dy ? (voz2 - voz1) / dy : 0;
      xl = (x0 << 8) + dx_long * (y1 - y0);
      xr = x1 << 8;
      izl = (iz0 << 8) + diz_long * (y1 - y0);
      izr = iz1 << 8;
      uozl = uoz0 + duoz_long * (y1 - y0);
      uozr = uoz1;
      vozl = voz0 + dvoz_long * (y1 - y0);
      vozr = voz1;
    }

    for (y = ya; y < yb; y++)
    {
      if (y >= 0 && y < VGA_HEIGHT)
      {
        int lx = xl >> 8, rx = xr >> 8;
        int liz = izl >> 8, riz = izr >> 8;
        int luoz = uozl, ruoz = uozr;
        int lvoz = vozl, rvoz = vozr;
        int sx, ex, off, x;
        int iz_curr, diz, uoz_curr, duoz, voz_curr, dvoz;
        int u_q816, v_q816, du_q816, dv_q816;
        int sub;

        if (lx > rx)
        {
          SWAP(lx, rx); SWAP(liz, riz);
          SWAP(luoz, ruoz); SWAP(lvoz, rvoz);
        }

        sx = lx < 0 ? 0 : lx;
        ex = rx >= VGA_WIDTH ? VGA_WIDTH - 1 : rx;

        if (sx <= ex)
        {
          int span = rx - lx;
          off = y * VGA_WIDTH;
          diz = span ? DIV_PLAIN(riz - liz, span) : 0;
          duoz = span ? (ruoz - luoz) / span : 0;
          dvoz = span ? (rvoz - lvoz) / span : 0;
          iz_curr = liz + diz * (sx - lx);
          uoz_curr = luoz + duoz * (sx - lx);
          voz_curr = lvoz + dvoz * (sx - lx);

          /* Initial perspective-correct anchor at sx, kept in Q?.16
           * (the integer part is u in Q8.8 = the texel-index source). */
          u_q816 = (iz_curr > 0 ? (uoz_curr / iz_curr) : 0) << 8;
          v_q816 = (iz_curr > 0 ? (voz_curr / iz_curr) : 0) << 8;
          du_q816 = 0;
          dv_q816 = 0;
          sub = 0;

          for (x = sx; x <= ex; x++)
          {
            if (sub == 0)
            {
              /* Look ahead `chunk` pixels and recompute the anchor
               * with a real perspective-correct divide; affine within
               * the chunk hides the divide cost. */
              int chunk = TEX_SUBDIV;
              int iz_ahead, uoz_ahead, voz_ahead;
              int u_ahead_q88, v_ahead_q88;
              if (x + chunk > ex) chunk = ex - x;
              if (chunk < 1) chunk = 1;

              iz_ahead = iz_curr + diz * chunk;
              uoz_ahead = uoz_curr + duoz * chunk;
              voz_ahead = voz_curr + dvoz * chunk;
              u_ahead_q88 = iz_ahead > 0 ? uoz_ahead / iz_ahead
                                         : (u_q816 >> 8);
              v_ahead_q88 = iz_ahead > 0 ? voz_ahead / iz_ahead
                                         : (v_q816 >> 8);
              du_q816 = ((u_ahead_q88 << 8) - u_q816) / chunk;
              dv_q816 = ((v_ahead_q88 << 8) - v_q816) / chunk;
              sub = chunk;
            }

            if (iz_curr > zb[off + x])
            {
              int tu = (u_q816 >> (uv_shift + 8)) & uv_mask;
              int tv = (v_q816 >> (uv_shift + 8)) & uv_mask;
              zb[off + x] = (unsigned short)iz_curr;
              buf[off + x] = texels[(tv << log2_size) + tu];
            }
            iz_curr += diz;
            uoz_curr += duoz;
            voz_curr += dvoz;
            u_q816 += du_q816;
            v_q816 += dv_q816;
            sub--;
          }
        }
      }
      xl += dx_long;
      xr += dx_short;
      izl += diz_long;
      izr += diz_short;
      uozl += duoz_long;
      uozr += duoz_short;
      vozl += dvoz_long;
      vozr += dvoz_short;
    }
  }
}

void fill_triangle_textured_gouraud(unsigned char *buf, unsigned short *zb,
                                    int x0, int y0, int z0,
                                    int u0, int v0, int i0,
                                    int x1, int y1, int z1,
                                    int u1, int v1, int i1,
                                    int x2, int y2, int z2,
                                    int u2, int v2, int i2,
                                    const Texture *tex,
                                    const Colormap *cm)
{
  int iz0, iz1, iz2;
  int uoz0, uoz1, uoz2, voz0, voz1, voz2;
  int dy_long, dx_long, diz_long, duoz_long, dvoz_long, di_long;
  int y, half;
  const unsigned char *texels = tex->pixels;
  const unsigned char *colormap = cm->map;
  int log2_size = tex->log2_size;
  int uv_mask = tex->size - 1;
  int uv_shift = 8 - log2_size;

  if (!recip_tab_ready)
    init_recip_tab();

  /* Sort by Y; UVs and intensity travel with each vertex. */
  if (y0 > y1)
  {
    SWAP(x0, x1); SWAP(y0, y1); SWAP(z0, z1);
    SWAP(u0, u1); SWAP(v0, v1); SWAP(i0, i1);
  }
  if (y1 > y2)
  {
    SWAP(x1, x2); SWAP(y1, y2); SWAP(z1, z2);
    SWAP(u1, u2); SWAP(v1, v2); SWAP(i1, i2);
  }
  if (y0 > y1)
  {
    SWAP(x0, x1); SWAP(y0, y1); SWAP(z0, z1);
    SWAP(u0, u1); SWAP(v0, v1); SWAP(i0, i1);
  }

  iz0 = (z0 > 0) ? (int)((1L << 20) / z0) : 0xFFFF;
  iz1 = (z1 > 0) ? (int)((1L << 20) / z1) : 0xFFFF;
  iz2 = (z2 > 0) ? (int)((1L << 20) / z2) : 0xFFFF;
  uoz0 = u0 * iz0; uoz1 = u1 * iz1; uoz2 = u2 * iz2;
  voz0 = v0 * iz0; voz1 = v1 * iz1; voz2 = v2 * iz2;

  /* 1-pixel early-out (degenerate). */
  if (y0 == y2 && x0 == x1 && x1 == x2)
  {
    if (x0 >= 0 && x0 < VGA_WIDTH && y0 >= 0 && y0 < VGA_HEIGHT)
    {
      int idx = y0 * VGA_WIDTH + x0;
      if (iz0 > zb[idx])
      {
        int tu = (u0 >> uv_shift) & uv_mask;
        int tv = (v0 >> uv_shift) & uv_mask;
        int texel = texels[(tv << log2_size) + tu];
        int level = i0 >> 2;
        if (level < 0) level = 0;
        else if (level > 63) level = 63;
        zb[idx] = (unsigned short)iz0;
        buf[idx] = colormap[(level << 8) | texel];
      }
    }
    return;
  }

  if (y0 == y2 || y2 < 0 || y0 >= VGA_HEIGHT)
    return;

  dy_long = y2 - y0;
  dx_long = DIV_SHIFTED(x2 - x0, dy_long);
  diz_long = DIV_SHIFTED(iz2 - iz0, dy_long);
  duoz_long = (uoz2 - uoz0) / dy_long;
  dvoz_long = (voz2 - voz0) / dy_long;
  /* Intensity in Q8.8 along edges, same precision as Gouraud raster. */
  di_long = ((i2 - i0) << 8) / dy_long;

  for (half = 0; half < 2; half++)
  {
    int ya, yb, dx_short, diz_short, duoz_short, dvoz_short, di_short;
    int xl, xr, izl, izr, uozl, uozr, vozl, vozr, il, ir;

    if (half == 0)
    {
      int dy = y1 - y0;
      ya = y0;
      yb = y1;
      dx_short = dy ? DIV_SHIFTED(x1 - x0, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz1 - iz0, dy) : 0;
      duoz_short = dy ? (uoz1 - uoz0) / dy : 0;
      dvoz_short = dy ? (voz1 - voz0) / dy : 0;
      di_short = dy ? ((i1 - i0) << 8) / dy : 0;
      xl = x0 << 8; xr = xl;
      izl = iz0 << 8; izr = izl;
      uozl = uoz0; uozr = uoz0;
      vozl = voz0; vozr = voz0;
      il = i0 << 8; ir = il;
    }
    else
    {
      int dy = y2 - y1;
      ya = y1;
      yb = y2;
      dx_short = dy ? DIV_SHIFTED(x2 - x1, dy) : 0;
      diz_short = dy ? DIV_SHIFTED(iz2 - iz1, dy) : 0;
      duoz_short = dy ? (uoz2 - uoz1) / dy : 0;
      dvoz_short = dy ? (voz2 - voz1) / dy : 0;
      di_short = dy ? ((i2 - i1) << 8) / dy : 0;
      xl = (x0 << 8) + dx_long * (y1 - y0);
      xr = x1 << 8;
      izl = (iz0 << 8) + diz_long * (y1 - y0);
      izr = iz1 << 8;
      uozl = uoz0 + duoz_long * (y1 - y0);
      uozr = uoz1;
      vozl = voz0 + dvoz_long * (y1 - y0);
      vozr = voz1;
      il = (i0 << 8) + di_long * (y1 - y0);
      ir = i1 << 8;
    }

    for (y = ya; y < yb; y++)
    {
      if (y >= 0 && y < VGA_HEIGHT)
      {
        int lx = xl >> 8, rx = xr >> 8;
        int liz = izl >> 8, riz = izr >> 8;
        int luoz = uozl, ruoz = uozr;
        int lvoz = vozl, rvoz = vozr;
        int li88 = il, ri88 = ir;
        int sx, ex, off, x;
        int iz_curr, diz, uoz_curr, duoz, voz_curr, dvoz;
        int u_q816, v_q816, du_q816, dv_q816;
        int i_q88, di_q88;
        int sub;

        if (lx > rx)
        {
          SWAP(lx, rx); SWAP(liz, riz);
          SWAP(luoz, ruoz); SWAP(lvoz, rvoz);
          SWAP(li88, ri88);
        }

        sx = lx < 0 ? 0 : lx;
        ex = rx >= VGA_WIDTH ? VGA_WIDTH - 1 : rx;

        if (sx <= ex)
        {
          int span = rx - lx;
          off = y * VGA_WIDTH;
          diz = span ? DIV_PLAIN(riz - liz, span) : 0;
          duoz = span ? (ruoz - luoz) / span : 0;
          dvoz = span ? (rvoz - lvoz) / span : 0;
          di_q88 = span ? DIV_PLAIN(ri88 - li88, span) : 0;
          iz_curr = liz + diz * (sx - lx);
          uoz_curr = luoz + duoz * (sx - lx);
          voz_curr = lvoz + dvoz * (sx - lx);
          i_q88 = li88 + di_q88 * (sx - lx);

          u_q816 = (iz_curr > 0 ? (uoz_curr / iz_curr) : 0) << 8;
          v_q816 = (iz_curr > 0 ? (voz_curr / iz_curr) : 0) << 8;
          du_q816 = 0;
          dv_q816 = 0;
          sub = 0;

          for (x = sx; x <= ex; x++)
          {
            if (sub == 0)
            {
              int chunk = TEX_SUBDIV;
              int iz_ahead, uoz_ahead, voz_ahead;
              int u_ahead_q88, v_ahead_q88;
              if (x + chunk > ex) chunk = ex - x;
              if (chunk < 1) chunk = 1;

              iz_ahead = iz_curr + diz * chunk;
              uoz_ahead = uoz_curr + duoz * chunk;
              voz_ahead = voz_curr + dvoz * chunk;
              u_ahead_q88 = iz_ahead > 0 ? uoz_ahead / iz_ahead
                                         : (u_q816 >> 8);
              v_ahead_q88 = iz_ahead > 0 ? voz_ahead / iz_ahead
                                         : (v_q816 >> 8);
              du_q816 = ((u_ahead_q88 << 8) - u_q816) / chunk;
              dv_q816 = ((v_ahead_q88 << 8) - v_q816) / chunk;
              sub = chunk;
            }

            if (iz_curr > zb[off + x])
            {
              int tu = (u_q816 >> (uv_shift + 8)) & uv_mask;
              int tv = (v_q816 >> (uv_shift + 8)) & uv_mask;
              int texel = texels[(tv << log2_size) + tu];
              /* intensity_q88 / 4 = level (0..63), clamped. */
              int level = i_q88 >> 10;
              if (level < 0) level = 0;
              else if (level > 63) level = 63;
              zb[off + x] = (unsigned short)iz_curr;
              buf[off + x] = colormap[(level << 8) | texel];
            }
            iz_curr += diz;
            uoz_curr += duoz;
            voz_curr += dvoz;
            u_q816 += du_q816;
            v_q816 += dv_q816;
            i_q88 += di_q88;
            sub--;
          }
        }
      }
      xl += dx_long;
      xr += dx_short;
      izl += diz_long;
      izr += diz_short;
      uozl += duoz_long;
      uozr += duoz_short;
      vozl += dvoz_long;
      vozr += dvoz_short;
      il += di_long;
      ir += di_short;
    }
  }
}
