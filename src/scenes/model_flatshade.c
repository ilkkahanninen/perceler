/*
 * Rotating teapot, flat-shaded with z-buffer and per-face Lambertian
 * lighting.  Loads positions + face normals only.
 */

#include "model_flatshade.h"

#include "../assets.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/model.h"
#include "utils/render3d.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

static const Camera3D camera = {
    INT_TO_FP(6),    /* cam_z */
    FP_ONE >> 2,     /* near_z */
    INT_TO_FP(200),  /* proj_scale */
    160, 100         /* cx, cy */
};

#define LIGHT_X (int)(0.41 * FP_ONE)
#define LIGHT_Y (int)(0.41 * FP_ONE)
#define LIGHT_Z (int)(-0.82 * FP_ONE)

static Model *model;
static int *transformed;        /* num_verts * 3 (positions) */
static int *transformed_fnorms; /* num_tris * 3 (face normals) */
static int *screen_xy;          /* num_verts * 2 */
static signed char *visible;    /* num_verts */
static unsigned short *zbuffer;
static int num_tris, num_verts;

static void set_palette(void)
{
  int i;
  for (i = 0; i < 256; i++)
    vga_setpalette((unsigned char)i, (unsigned char)(i >> 2),
                   (unsigned char)(i >> 2), (unsigned char)(i >> 2));
}

static int lambert(const int *n)
{
  int dot = FP_MUL(n[0], LIGHT_X) + FP_MUL(n[1], LIGHT_Y) +
            FP_MUL(n[2], LIGHT_Z);
  dot = FP_TO_INT(dot * 128) + 128;
  if (dot < 8) dot = 8;
  if (dot > 255) dot = 255;
  return dot;
}

static void setup(void)
{
  model = model_load(ASSET_TEAPOT_MDL, MODEL_FLAT);
  num_tris = model->num_triangles;
  num_verts = model->num_vertices;
  transformed = (int *)malloc(num_verts * 3 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  screen_xy = (int *)malloc(num_verts * 2 * sizeof(int));
  visible = (signed char *)malloc(num_verts);
  zbuffer = (unsigned short *)malloc(VGA_SIZE * sizeof(unsigned short));
}

static void init(const RenderContext *ctx)
{
  (void)ctx;
  set_palette();
}

static void shutdown(void)
{
  model_free(model);
  model = NULL;
  free(transformed);
  transformed = NULL;
  free(transformed_fnorms);
  transformed_fnorms = NULL;
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
  const int *indices = model->indices;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_points(transformed, model->positions, num_verts, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, model->face_normals, num_tris, ay, ax);
  project_points(&camera, transformed, num_verts, screen_xy, visible);
  memset(zbuffer, 0, VGA_SIZE * sizeof(unsigned short));

  for (i = 0; i < num_tris; i++)
  {
    int i0 = indices[i * 3 + 0];
    int i1 = indices[i * 3 + 1];
    int i2 = indices[i * 3 + 2];
    int *v0 = transformed + i0 * 3;
    int *v1 = transformed + i1 * 3;
    int *v2 = transformed + i2 * 3;
    int *nr = transformed_fnorms + i * 3;

    if (backface3d(nr, v0))
      continue;
    if (!visible[i0] || !visible[i1] || !visible[i2])
      continue;

    fill_triangle_flat(backbuffer, zbuffer,
                       screen_xy[i0 * 2], screen_xy[i0 * 2 + 1], v0[2],
                       screen_xy[i1 * 2], screen_xy[i1 * 2 + 1], v1[2],
                       screen_xy[i2 * 2], screen_xy[i2 * 2 + 1], v2[2],
                       (unsigned char)lambert(nr));
  }

  font_draw(&font_default, backbuffer, 4, 4, 255,
            "model_flatshade.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene model_flatshade_scene = {setup, init, shutdown, render};
