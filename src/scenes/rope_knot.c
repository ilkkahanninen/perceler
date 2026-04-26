/*
 * Trefoil-knot path swept into a 3-stranded rope by rope_create().
 * Demonstrates the tube/rope sweep generator with a non-trivial
 * 3D path.
 */

#include "rope_knot.h"

#include "../assets.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/model.h"
#include "utils/palette.h"
#include "utils/polyhedron.h" /* POLYHEDRON_SMOOTH */
#include "utils/render3d.h"
#include "utils/tube.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vga.h>

static const Camera3D camera = {
    INT_TO_FP(3),   /* cam_z */
    FP_ONE >> 2,    /* near_z */
    INT_TO_FP(180), /* proj_scale */
    160, 100        /* cx, cy */
};

#define PATH_POINTS 96
#define TWO_PI 6.28318530717958647692f

static Model *rope;
static Texture *texture;
static int *transformed;
static int *transformed_fnorms;
static int *transformed_vnorms;
static unsigned short *zbuffer;
static int num_tris;

/* Build a trefoil-knot polyline, scaled to fit the view frustum. */
static int *build_path(void)
{
  int *path = (int *)malloc(PATH_POINTS * 3 * sizeof(int));
  int i;
  if (!path)
    return NULL;
  for (i = 0; i < PATH_POINTS; i++)
  {
    float t = (float)i * TWO_PI / (float)(PATH_POINTS - 1);
    float x = ((float)sin((double)t) + 2.0f * (float)sin((double)(2.0f * t))) * 0.30f;
    float y = ((float)cos((double)t) - 2.0f * (float)cos((double)(2.0f * t))) * 0.30f;
    float z = -(float)sin((double)(3.0f * t)) * 0.30f;
    path[i * 3 + 0] = (int)(x * 256.0f);
    path[i * 3 + 1] = (int)(y * 256.0f);
    path[i * 3 + 2] = (int)(z * 256.0f);
  }
  return path;
}

static void setup(void)
{
  int *path = build_path();
  rope = rope_create(path, PATH_POINTS, INT_TO_FP(1) / 8, 3, 0,
                     POLYHEDRON_SMOOTH);
  free(path);
  texture = texture_load(ASSET_LANDSCAPE_BMP);
  num_tris = rope->num_triangles;
  transformed = (int *)malloc(num_tris * 9 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  transformed_vnorms = (int *)malloc(num_tris * 9 * sizeof(int));
  zbuffer = (unsigned short *)malloc(VGA_SIZE * sizeof(unsigned short));
}

static void init(const RenderContext *ctx)
{
  (void)ctx;
  palette_apply(&texture->palette);
}

static void shutdown(void)
{
  model_free(rope);
  rope = NULL;
  texture_free(texture);
  texture = NULL;
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
  transform_points(transformed, rope->positions, num_tris * 3, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, rope->face_normals, num_tris, ay, ax);
  transform_dirs(transformed_vnorms, rope->vertex_normals, num_tris * 3,
                 ay, ax);
  memset(zbuffer, 0, VGA_SIZE * sizeof(unsigned short));

  for (i = 0; i < num_tris; i++)
  {
    int *v = transformed + i * 9;
    int *fn = transformed_fnorms + i * 3;
    int *vn = transformed_vnorms + i * 9;
    int sx0, sy0, sx1, sy1, sx2, sy2;
    int u0, v0_uv, u1, v1_uv, u2, v2_uv;

    if (backface3d(fn, v))
      continue;

    if (!project3d(&camera, v[0], v[1], v[2], &sx0, &sy0) ||
        !project3d(&camera, v[3], v[4], v[5], &sx1, &sy1) ||
        !project3d(&camera, v[6], v[7], v[8], &sx2, &sy2))
      continue;

    sphere_map_uv(vn[0], vn[1], &u0, &v0_uv);
    sphere_map_uv(vn[3], vn[4], &u1, &v1_uv);
    sphere_map_uv(vn[6], vn[7], &u2, &v2_uv);

    fill_triangle_textured_affine(backbuffer, zbuffer,
                                  sx0, sy0, v[2], u0, v0_uv,
                                  sx1, sy1, v[5], u1, v1_uv,
                                  sx2, sy2, v[8], u2, v2_uv,
                                  texture);
  }

  font_draw(&font_default, backbuffer, 4, 4, 255, "rope_knot.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene rope_knot_scene = {setup, init, shutdown, render};
