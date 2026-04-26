/*
 * Textured cube with per-vertex Lambert shading via a colormap LUT.
 * The texture texture's palette is uploaded to the DAC and a 64×256
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
static Texture *texture;
static Colormap *colormap;
static int *transformed;        /* num_verts * 3 */
static int *transformed_fnorms; /* num_tris * 3 */
static int *transformed_vnorms; /* num_verts * 3 */
static int *vertex_lambert;     /* num_verts */
static int *screen_xy;          /* num_verts * 2 */
static signed char *visible;    /* num_verts */
static unsigned short *zbuffer;
static int num_tris, num_verts;

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
  cube = polyhedron_create(POLYHEDRON_CUBE, 0, 0, 0);
  texture = texture_load(ASSET_MARBLE_BMP);
  /* Colormap is 16 KB — heap is fine. */
  colormap = (Colormap *)malloc(sizeof(Colormap));
  colormap_build(colormap, &texture->palette);
  num_tris = cube->num_triangles;
  num_verts = cube->num_vertices;
  transformed = (int *)malloc(num_verts * 3 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  transformed_vnorms = (int *)malloc(num_verts * 3 * sizeof(int));
  vertex_lambert = (int *)malloc(num_verts * sizeof(int));
  screen_xy = (int *)malloc(num_verts * 2 * sizeof(int));
  visible = (signed char *)malloc(num_verts);
  zbuffer = (unsigned short *)malloc(VGA_SIZE * sizeof(unsigned short));
}

static void init(const RenderContext *ctx)
{
  (void)ctx;
  palette_apply(&texture->palette);
}

static void shutdown(void)
{
  model_free(cube);
  cube = NULL;
  texture_free(texture);
  texture = NULL;
  free(colormap);
  colormap = NULL;
  free(transformed);
  transformed = NULL;
  free(transformed_fnorms);
  transformed_fnorms = NULL;
  free(transformed_vnorms);
  transformed_vnorms = NULL;
  free(vertex_lambert);
  vertex_lambert = NULL;
  free(screen_xy);
  screen_xy = NULL;
  free(visible);
  visible = NULL;
  free(zbuffer);
  zbuffer = NULL;
}

static void render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned char ay = (unsigned char)ctx->frame;
  unsigned char ax = (unsigned char)(ctx->frame >> 1);
  const int *indices = cube->indices;
  const int *uvs = cube->uvs;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_points(transformed, cube->positions, num_verts, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, cube->face_normals, num_tris, ay, ax);
  transform_dirs(transformed_vnorms, cube->vertex_normals, num_verts,
                 ay, ax);
  project_points(&camera, transformed, num_verts, screen_xy, visible);
  for (i = 0; i < num_verts; i++)
    vertex_lambert[i] = lambert(transformed_vnorms + i * 3);
  memset(zbuffer, 0, VGA_SIZE * sizeof(unsigned short));

  for (i = 0; i < num_tris; i++)
  {
    int i0 = indices[i * 3 + 0];
    int i1 = indices[i * 3 + 1];
    int i2 = indices[i * 3 + 2];
    int *v0 = transformed + i0 * 3;
    int *v1 = transformed + i1 * 3;
    int *v2 = transformed + i2 * 3;
    int *fn = transformed_fnorms + i * 3;

    if (backface3d(fn, v0))
      continue;
    if (!visible[i0] || !visible[i1] || !visible[i2])
      continue;

    fill_triangle_textured_gouraud(backbuffer, zbuffer,
        screen_xy[i0 * 2], screen_xy[i0 * 2 + 1], v0[2],
        uvs[i0 * 2], uvs[i0 * 2 + 1], vertex_lambert[i0],
        screen_xy[i1 * 2], screen_xy[i1 * 2 + 1], v1[2],
        uvs[i1 * 2], uvs[i1 * 2 + 1], vertex_lambert[i1],
        screen_xy[i2 * 2], screen_xy[i2 * 2 + 1], v2[2],
        uvs[i2 * 2], uvs[i2 * 2 + 1], vertex_lambert[i2],
        texture, colormap);
  }

  font_draw(&font_default, backbuffer, 4, 4, 255, "shaded_cube.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene shaded_cube_scene = {setup, init, shutdown, render};
