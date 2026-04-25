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
static int *transformed;        /* num_tris * 9 (positions) */
static int *transformed_fnorms; /* num_tris * 3 (face normals) */
static int num_tris;

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
  transformed = (int *)malloc(num_tris * 9 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
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
}

static void render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned char ay = (unsigned char)ctx->frame;
  unsigned char ax = (unsigned char)(ctx->frame >> 1);
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_points(transformed, model->positions, num_tris * 3, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, model->face_normals, num_tris, ay, ax);

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int *nr = transformed_fnorms + i * 3;
    int sx0, sy0, sx1, sy1, sx2, sy2;

    if (backface3d(nr, v))
      continue;

    if (!project3d(&camera, v[0], v[1], v[2], &sx0, &sy0) ||
        !project3d(&camera, v[3], v[4], v[5], &sx1, &sy1) ||
        !project3d(&camera, v[6], v[7], v[8], &sx2, &sy2))
      continue;

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
