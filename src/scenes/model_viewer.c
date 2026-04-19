/*
 * Model viewer - wireframe and flat-shaded 3D model with rotation
 */

#include "model_viewer.h"

#include "../assets.h"
#include "utils/draw.h"
#include "utils/math.h"
#include "utils/model.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

#define CAM_Z INT_TO_FP(6)
#define NEAR_Z (FP_ONE >> 2)
#define CX (160)
#define CY (100)
#define PROJ_SCALE INT_TO_FP(200)

#define LIGHT_X (int)(0.41 * FP_ONE)
#define LIGHT_Y (int)(0.41 * FP_ONE)
#define LIGHT_Z (int)(-0.82 * FP_ONE)

static Model *model;
static int *transformed;
static int *transformed_normals;
static int *zbuffer;
static int num_tris;

static void set_palette(void)
{
  int i;
  for (i = 0; i < 256; i++)
    vga_setpalette((unsigned char)i, (unsigned char)(i >> 2),
                   (unsigned char)(i >> 2), (unsigned char)(i >> 2));
}

static void model_viewer_setup(void)
{
  model = model_load(ASSET_TEAPOT_MDL);
  num_tris = model->num_triangles;
  transformed = (int *)malloc(num_tris * 9 * sizeof(int));
  transformed_normals = (int *)malloc(num_tris * 3 * sizeof(int));
  zbuffer = (int *)malloc(VGA_SIZE * sizeof(int));
}

static void model_viewer_init(unsigned char *backbuffer)
{
  (void)backbuffer;
  set_palette();
}

static void model_viewer_shutdown(void)
{
  model_free(model);
  model = NULL;
  free(transformed);
  transformed = NULL;
  free(transformed_normals);
  transformed_normals = NULL;
  free(zbuffer);
  zbuffer = NULL;
}

static inline int project(int x, int y, int z, int *sx, int *sy)
{
  if (z < NEAR_Z)
    return 0;
  *sx = CX + FP_TO_INT(FP_MUL(FP_DIV(x, z), PROJ_SCALE));
  *sy = CY - FP_TO_INT(FP_MUL(FP_DIV(y, z), PROJ_SCALE));
  return 1;
}

static inline int backface(int sx0, int sy0, int sx1, int sy1,
                           int sx2, int sy2)
{
  return (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0) <= 0;
}

static void transform_vertices(unsigned int frame)
{
  unsigned char angle_y = (unsigned char)(frame);
  unsigned char angle_x = (unsigned char)(frame >> 1);
  int sin_y = sin8(angle_y), cos_y = cos8(angle_y);
  int sin_x = sin8(angle_x), cos_x = cos8(angle_x);
  const int *src = model->positions;
  int *dst = transformed;
  int i, num_verts = num_tris * 3;

  for (i = 0; i < num_verts; i++, src += 3, dst += 3)
  {
    int x = src[0], y = src[1], z = src[2];
    int rx = FP_MUL(x, cos_y) + FP_MUL(z, sin_y);
    int ry = y;
    int rz = FP_MUL(-x, sin_y) + FP_MUL(z, cos_y);

    dst[0] = rx;
    dst[1] = FP_MUL(ry, cos_x) - FP_MUL(rz, sin_x);
    dst[2] = FP_MUL(ry, sin_x) + FP_MUL(rz, cos_x) + CAM_Z;
  }
}

static void transform_normals(unsigned int frame)
{
  unsigned char angle_y = (unsigned char)(frame);
  unsigned char angle_x = (unsigned char)(frame >> 1);
  int sin_y = sin8(angle_y), cos_y = cos8(angle_y);
  int sin_x = sin8(angle_x), cos_x = cos8(angle_x);
  const int *src = model->normals;
  int *dst = transformed_normals;
  int i;

  for (i = 0; i < num_tris; i++, src += 3, dst += 3)
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

/* --- Wireframe renderer --- */

static void model_viewer_wireframe_render(unsigned char *backbuffer,
                                          unsigned int frame,
                                          unsigned int timeline_frame)
{
  (void)timeline_frame;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_vertices(frame);

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int sx0, sy0, sx1, sy1, sx2, sy2;

    if (!project(v[0], v[1], v[2], &sx0, &sy0) ||
        !project(v[3], v[4], v[5], &sx1, &sy1) ||
        !project(v[6], v[7], v[8], &sx2, &sy2))
      continue;
    if (backface(sx0, sy0, sx1, sy1, sx2, sy2))
      continue;

    draw_line(backbuffer, sx0, sy0, sx1, sy1, 255);
    draw_line(backbuffer, sx1, sy1, sx2, sy2, 255);
    draw_line(backbuffer, sx2, sy2, sx0, sy0, 255);
  }

  vga_vsync();
  vga_blit(backbuffer);
}

/* --- Z-buffered flat-shaded triangle rasterizer --- */

static void fill_triangle_z(unsigned char *buf, int *zb,
                            int x0, int y0, int z0,
                            int x1, int y1, int z1,
                            int x2, int y2, int z2,
                            unsigned char color)
{
  int iz0, iz1, iz2;
  int dy_long, dx_long, diz_long;
  int y, half;

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

  if (y0 == y2 || y2 < 0 || y0 >= VGA_HEIGHT)
    return;

  iz0 = (z0 > 0) ? (int)((1L << 20) / z0) : 0x7FFF;
  iz1 = (z1 > 0) ? (int)((1L << 20) / z1) : 0x7FFF;
  iz2 = (z2 > 0) ? (int)((1L << 20) / z2) : 0x7FFF;

  dy_long = y2 - y0;
  dx_long = ((x2 - x0) << 8) / dy_long;
  diz_long = ((iz2 - iz0) << 8) / dy_long;

  for (half = 0; half < 2; half++)
  {
    int ya, yb, dy, dx_short, diz_short;
    int xl, xr, izl, izr;

    if (half == 0)
    {
      ya = y0;
      yb = y1;
      dx_short = (y1 != y0) ? ((x1 - x0) << 8) / (y1 - y0) : 0;
      diz_short = (y1 != y0) ? ((iz1 - iz0) << 8) / (y1 - y0) : 0;
      xl = x0 << 8;
      xr = xl;
      izl = iz0 << 8;
      izr = izl;
    }
    else
    {
      ya = y1;
      yb = y2;
      dx_short = (y2 != y1) ? ((x2 - x1) << 8) / (y2 - y1) : 0;
      diz_short = (y2 != y1) ? ((iz2 - iz1) << 8) / (y2 - y1) : 0;
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
          off = y * VGA_WIDTH;
          diz = (rx != lx) ? (riz - liz) / (rx - lx) : 0;
          iz = liz + diz * (sx - lx);

          for (x = sx; x <= ex; x++)
          {
            if (iz > zb[off + x])
            {
              zb[off + x] = iz;
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

/* --- Flat-shaded renderer --- */

static void model_viewer_flatshade_render(unsigned char *backbuffer,
                                          unsigned int frame,
                                          unsigned int timeline_frame)
{
  (void)timeline_frame;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_vertices(frame);
  transform_normals(frame);
  memset(zbuffer, 0, VGA_SIZE * sizeof(int));

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int *nr = transformed_normals + i * 3;
    int sx0, sy0, sx1, sy1, sx2, sy2;
    int dot;

    if (!project(v[0], v[1], v[2], &sx0, &sy0) ||
        !project(v[3], v[4], v[5], &sx1, &sy1) ||
        !project(v[6], v[7], v[8], &sx2, &sy2))
      continue;
    if (backface(sx0, sy0, sx1, sy1, sx2, sy2))
      continue;

    dot = FP_MUL(nr[0], LIGHT_X) + FP_MUL(nr[1], LIGHT_Y) +
          FP_MUL(nr[2], LIGHT_Z);
    dot = FP_TO_INT(dot * 128) + 128;
    if (dot < 8)
      dot = 8;
    if (dot > 255)
      dot = 255;

    fill_triangle_z(backbuffer, zbuffer,
                    sx0, sy0, v[2], sx1, sy1, v[5], sx2, sy2, v[8],
                    (unsigned char)dot);
  }

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene model_viewer_scene = {model_viewer_setup, model_viewer_init,
                                  model_viewer_shutdown, model_viewer_wireframe_render};

const Scene model_viewer_flatshade_scene = {
    model_viewer_setup, model_viewer_init, model_viewer_shutdown,
    model_viewer_flatshade_render};
