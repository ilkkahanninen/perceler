/*
 * Model viewer - wireframe 3D model with Y-axis rotation
 */

#include "model_viewer.h"

#include "../assets.h"
#include "utils/draw.h"
#include "utils/math.h"
#include "utils/model.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

/* Camera distance from origin (8.8 fp) */
#define CAM_Z INT_TO_FP(6)

/* Near plane to avoid division by zero */
#define NEAR_Z (FP_ONE >> 2)

/* Screen center */
#define CX (VGA_WIDTH / 2)
#define CY (VGA_HEIGHT / 2)

/* Projection scale (8.8 fp) */
#define PROJ_SCALE INT_TO_FP(200)

static Model *model;
static int *transformed; /* num_triangles * 9: transformed positions */

static void set_palette(void)
{
  int i;
  for (i = 0; i < 256; i++)
  {
    unsigned char c = (unsigned char)(i >> 2);
    vga_setpalette((unsigned char)i, c, c, c);
  }
}

static void model_viewer_setup(void)
{
  model = model_load(ASSET_TEAPOT_MDL);
  transformed = (int *)malloc(model->num_triangles * 9 * sizeof(int));
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
}

/* Project an 8.8 3D point to screen coordinates.
 * Returns 0 if behind the near plane. */
static int project(int x, int y, int z, int *sx, int *sy)
{
  if (z < NEAR_Z)
    return 0;
  *sx = CX + FP_TO_INT(FP_MUL(FP_DIV(x, z), PROJ_SCALE));
  *sy = CY - FP_TO_INT(FP_MUL(FP_DIV(y, z), PROJ_SCALE));
  return 1;
}

static void model_viewer_render(unsigned char *backbuffer, unsigned int frame)
{
  int i;
  unsigned char angle_y = (unsigned char)(frame);
  unsigned char angle_x = (unsigned char)(frame >> 1);
  int sin_y = sin8(angle_y);
  int cos_y = cos8(angle_y);
  int sin_x = sin8(angle_x);
  int cos_x = cos8(angle_x);
  const int *src = model->positions;
  int *dst = transformed;
  int n = model->num_triangles;

  /* Clear */
  memset(backbuffer, 0, VGA_SIZE);

  /* Transform vertices: rotate Y then X, then translate along Z */
  for (i = 0; i < n * 3; i++)
  {
    int x = src[i * 3];
    int y = src[i * 3 + 1];
    int z = src[i * 3 + 2];
    int rx, ry, rz;

    /* Rotate around Y */
    rx = FP_MUL(x, cos_y) + FP_MUL(z, sin_y);
    ry = y;
    rz = FP_MUL(-x, sin_y) + FP_MUL(z, cos_y);

    /* Rotate around X */
    dst[i * 3] = rx;
    dst[i * 3 + 1] = FP_MUL(ry, cos_x) - FP_MUL(rz, sin_x);
    dst[i * 3 + 2] = FP_MUL(ry, sin_x) + FP_MUL(rz, cos_x) + CAM_Z;
  }

  /* Draw wireframe triangles */
  for (i = 0; i < n; i++)
  {
    int *v = dst + i * 9;
    int sx0, sy0, sx1, sy1, sx2, sy2;

    if (!project(v[0], v[1], v[2], &sx0, &sy0))
      continue;
    if (!project(v[3], v[4], v[5], &sx1, &sy1))
      continue;
    if (!project(v[6], v[7], v[8], &sx2, &sy2))
      continue;

    /* Back-face culling in screen space */
    {
      int ax = sx1 - sx0;
      int ay = sy1 - sy0;
      int bx = sx2 - sx0;
      int by = sy2 - sy0;
      if (ax * by - ay * bx >= 0)
        continue;
    }

    draw_line(backbuffer, sx0, sy0, sx1, sy1, 255);
    draw_line(backbuffer, sx1, sy1, sx2, sy2, 255);
    draw_line(backbuffer, sx2, sy2, sx0, sy0, 255);
  }

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene model_viewer_scene = {model_viewer_setup, model_viewer_init,
                                  model_viewer_shutdown, model_viewer_render};
