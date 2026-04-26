/*
 * Rotating teapot, wireframe.  Loads positions + face normals only
 * (the latter for backface culling); skips UVs and vertex normals.
 */

#include "model_wireframe.h"

#include "../assets.h"
#include "utils/draw.h"
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

static Model *model;
static int *transformed;        /* num_verts * 3 (positions) */
static int *transformed_fnorms; /* num_tris * 3 (face normals) */
static int *screen_xy;          /* num_verts * 2 */
static signed char *visible;    /* num_verts */
static int num_tris, num_verts;

static void set_palette(void)
{
  int i;
  for (i = 0; i < 256; i++)
    vga_setpalette((unsigned char)i, (unsigned char)(i >> 2),
                   (unsigned char)(i >> 2), (unsigned char)(i >> 2));
}

static void setup(void)
{
  model = model_load(ASSET_TEAPOT_MDL, MODEL_WIREFRAME);
  num_tris = model->num_triangles;
  num_verts = model->num_vertices;
  transformed = (int *)malloc(num_verts * 3 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  screen_xy = (int *)malloc(num_verts * 2 * sizeof(int));
  visible = (signed char *)malloc(num_verts);
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

  for (i = 0; i < num_tris; i++)
  {
    int i0 = indices[i * 3 + 0];
    int i1 = indices[i * 3 + 1];
    int i2 = indices[i * 3 + 2];
    int *v0 = transformed + i0 * 3;
    int *nr = transformed_fnorms + i * 3;
    int sx0, sy0, sx1, sy1, sx2, sy2;

    if (backface3d(nr, v0))
      continue;
    if (!visible[i0] || !visible[i1] || !visible[i2])
      continue;

    sx0 = screen_xy[i0 * 2]; sy0 = screen_xy[i0 * 2 + 1];
    sx1 = screen_xy[i1 * 2]; sy1 = screen_xy[i1 * 2 + 1];
    sx2 = screen_xy[i2 * 2]; sy2 = screen_xy[i2 * 2 + 1];

    draw_line(backbuffer, sx0, sy0, sx1, sy1, 255);
    draw_line(backbuffer, sx1, sy1, sx2, sy2, 255);
    draw_line(backbuffer, sx2, sy2, sx0, sy0, 255);
  }

  font_draw(&font_default, backbuffer, 4, 4, 255,
            "model_wireframe.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene model_wireframe_scene = {setup, init, shutdown, render};
