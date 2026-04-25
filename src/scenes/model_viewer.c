/*
 * Model viewer - wireframe and flat-shaded 3D model with rotation
 */

#include "model_viewer.h"

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

#define LIGHT_X (int)(0.41 * FP_ONE)
#define LIGHT_Y (int)(0.41 * FP_ONE)
#define LIGHT_Z (int)(-0.82 * FP_ONE)

static Model *model;
static int *transformed;
static int *transformed_normals;
static unsigned short *zbuffer;
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
  zbuffer = (unsigned short *)malloc(VGA_SIZE * sizeof(unsigned short));
}

static void model_viewer_init(const RenderContext *ctx)
{
  (void)ctx;
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

static void rotate_model(unsigned int frame)
{
  unsigned char ay = (unsigned char)(frame);
  unsigned char ax = (unsigned char)(frame >> 1);
  transform_points(transformed, model->positions, num_tris * 3, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_normals, model->normals, num_tris, ay, ax);
}

/* --- Wireframe renderer --- */

static void model_viewer_wireframe_render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  rotate_model(ctx->frame);

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int *nr = transformed_normals + i * 3;
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

  font_draw(&font_default, backbuffer, 4, 4, 255, "model_viewer.c - Wireframe");

  vga_vsync();
  vga_blit(backbuffer);
}

/* --- Flat-shaded renderer --- */

static void model_viewer_flatshade_render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  rotate_model(ctx->frame);
  memset(zbuffer, 0, VGA_SIZE * sizeof(unsigned short));

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int *nr = transformed_normals + i * 3;
    int sx0, sy0, sx1, sy1, sx2, sy2;
    int dot;

    if (backface3d(nr, v))
      continue;

    if (!project3d(&camera, v[0], v[1], v[2], &sx0, &sy0) ||
        !project3d(&camera, v[3], v[4], v[5], &sx1, &sy1) ||
        !project3d(&camera, v[6], v[7], v[8], &sx2, &sy2))
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

  font_draw(&font_default, backbuffer, 4, 4, 255, "model_viewer.c - Flat-shaded");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene model_viewer_scene = {model_viewer_setup, model_viewer_init,
                                  model_viewer_shutdown, model_viewer_wireframe_render};

const Scene model_viewer_flatshade_scene = {
    model_viewer_setup, model_viewer_init, model_viewer_shutdown,
    model_viewer_flatshade_render};
