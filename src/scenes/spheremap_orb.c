/*
 * Sphere-mapped orb: extruded icosahedron built with averaged vertex
 * normals, so sphere-mapping produces continuous UVs across each
 * triangle. Combined with the affine textured rasterizer, the marble
 * texture is draped smoothly over the rounded gem.
 */

#include "spheremap_orb.h"

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
    INT_TO_FP(3),   /* cam_z */
    FP_ONE >> 2,    /* near_z */
    INT_TO_FP(220), /* proj_scale */
    160, 100        /* cx, cy */
};

static Model *orb;
static Texture *texture;
static int *transformed;        /* num_tris * 9 (positions) */
static int *transformed_fnorms; /* num_tris * 3 (face normals, for cull) */
static int *transformed_vnorms; /* num_tris * 9 (vertex normals) */
static unsigned short *zbuffer;
static int num_tris;

static void setup(void)
{
  orb = polyhedron_create(POLYHEDRON_ICOSAHEDRON,
                          INT_TO_FP(1) / 8, INT_TO_FP(3) / 4,
                          POLYHEDRON_SMOOTH);
  texture = texture_load(ASSET_LANDSCAPE_BMP);
  num_tris = orb->num_triangles;
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
  model_free(orb);
  orb = NULL;
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
  transform_points(transformed, orb->positions, num_tris * 3, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, orb->face_normals, num_tris, ay, ax);
  transform_dirs(transformed_vnorms, orb->vertex_normals, num_tris * 3,
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

  font_draw(&font_default, backbuffer, 4, 4, 255, "spheremap_orb.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene spheremap_orb_scene = {setup, init, shutdown, render};
