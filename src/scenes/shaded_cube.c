/*
 * Textured cube with per-vertex Lambert shading via a colormap LUT.
 * The marble texture's palette is uploaded to the DAC and a 64×256
 * colormap is precomputed at setup so the rasterizer can shade each
 * texel with a single byte lookup — no per-pixel multiply.
 */

#include "shaded_cube.h"

#include "../assets.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/model.h"
#include "utils/palette.h"
#include "utils/polyhedron.h"
#include "utils/render3d.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

static const Camera3D camera = {
    INT_TO_FP(4),   /* cam_z */
    FP_ONE >> 2,    /* near_z */
    INT_TO_FP(220), /* proj_scale */
    160, 100        /* cx, cy */
};

#define LIGHT_X (int)(0.41 * FP_ONE)
#define LIGHT_Y (int)(0.41 * FP_ONE)
#define LIGHT_Z (int)(0.82 * FP_ONE)

static Model *cube;
static Texture *marble;
static Colormap *colormap;
static int *transformed;        /* num_tris * 9 (positions) */
static int *transformed_fnorms; /* num_tris * 3 (face normals, for cull) */
static int *transformed_vnorms; /* num_tris * 9 (vertex normals) */
static unsigned short *zbuffer;
static int num_tris;

static int lambert(const int *n)
{
  int dot = FP_MUL(n[0], LIGHT_X) + FP_MUL(n[1], LIGHT_Y) +
            FP_MUL(n[2], LIGHT_Z);
  dot = FP_TO_INT(dot * 128) + 128;
  if (dot < 8)
    dot = 8;
  if (dot > 255)
    dot = 255;
  return dot;
}

static void setup(void)
{
  cube = polyhedron_create(POLYHEDRON_CUBE, 0, 0);
  marble = texture_load(ASSET_MARBLE_BMP);
  /* Colormap is 16 KB — heap is fine. */
  colormap = (Colormap *)malloc(sizeof(Colormap));
  colormap_build(colormap, &marble->palette);
  num_tris = cube->num_triangles;
  transformed = (int *)malloc(num_tris * 9 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  transformed_vnorms = (int *)malloc(num_tris * 9 * sizeof(int));
  zbuffer = (unsigned short *)malloc(VGA_SIZE * sizeof(unsigned short));
}

static void init(const RenderContext *ctx)
{
  (void)ctx;
  palette_apply(&marble->palette);
}

static void shutdown(void)
{
  model_free(cube);
  cube = NULL;
  texture_free(marble);
  marble = NULL;
  free(colormap);
  colormap = NULL;
  free(transformed);
  transformed = NULL;
  free(transformed_fnorms);
  transformed_fnorms = NULL;
  free(transformed_vnorms);
  transformed_vnorms = NULL;
  free(zbuffer);
  zbuffer = NULL;
}

static void render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned char ay = (unsigned char)ctx->frame;
  unsigned char ax = (unsigned char)(ctx->frame >> 1);
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_points(transformed, cube->positions, num_tris * 3, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, cube->face_normals, num_tris, ay, ax);
  transform_dirs(transformed_vnorms, cube->vertex_normals, num_tris * 3,
                 ay, ax);
  memset(zbuffer, 0, VGA_SIZE * sizeof(unsigned short));

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int *fn = transformed_fnorms + i * 3;
    int *vn = transformed_vnorms + i * 9;
    int *uv = cube->uvs + i * 6;
    int sx0, sy0, sx1, sy1, sx2, sy2;

    if (backface3d(fn, v))
      continue;

    if (!project3d(&camera, v[0], v[1], v[2], &sx0, &sy0) ||
        !project3d(&camera, v[3], v[4], v[5], &sx1, &sy1) ||
        !project3d(&camera, v[6], v[7], v[8], &sx2, &sy2))
      continue;

    fill_triangle_textured_gouraud(backbuffer, zbuffer,
                                   sx0, sy0, v[2], uv[0], uv[1], lambert(vn + 0),
                                   sx1, sy1, v[5], uv[2], uv[3], lambert(vn + 3),
                                   sx2, sy2, v[8], uv[4], uv[5], lambert(vn + 6),
                                   marble, colormap);
  }

  font_draw(&font_default, backbuffer, 4, 4, 255, "shaded_cube.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene shaded_cube_scene = {setup, init, shutdown, render};
