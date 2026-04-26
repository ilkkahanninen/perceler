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
static int *transformed;        /* num_verts * 3 (positions) */
static int *transformed_fnorms; /* num_tris * 3 (face normals, for cull) */
static int *transformed_vnorms; /* num_verts * 3 (vertex normals) */
static int *vertex_uv;          /* num_verts * 2 (sphere-mapped UVs) */
static int *screen_xy;          /* num_verts * 2 */
static signed char *visible;    /* num_verts */
static unsigned short *zbuffer;
static int num_tris, num_verts;

static void setup(void)
{
  orb = polyhedron_create(POLYHEDRON_ICOSAHEDRON,
                          INT_TO_FP(1) / 8, INT_TO_FP(3) / 4,
                          POLYHEDRON_SMOOTH);
  texture = texture_load(ASSET_LANDSCAPE_BMP);
  num_tris = orb->num_triangles;
  num_verts = orb->num_vertices;
  transformed = (int *)malloc(num_verts * 3 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  transformed_vnorms = (int *)malloc(num_verts * 3 * sizeof(int));
  vertex_uv = (int *)malloc(num_verts * 2 * sizeof(int));
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
  free(vertex_uv);
  vertex_uv = NULL;
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
  const int *indices = orb->indices;
  int i;

  memset(backbuffer, 0, VGA_SIZE);
  transform_points(transformed, orb->positions, num_verts, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, orb->face_normals, num_tris, ay, ax);
  transform_dirs(transformed_vnorms, orb->vertex_normals, num_verts,
                 ay, ax);
  project_points(&camera, transformed, num_verts, screen_xy, visible);
  for (i = 0; i < num_verts; i++)
    sphere_map_uv(transformed_vnorms[i * 3 + 0], transformed_vnorms[i * 3 + 1],
                  &vertex_uv[i * 2 + 0], &vertex_uv[i * 2 + 1]);
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

    fill_triangle_textured_affine(backbuffer, zbuffer,
        screen_xy[i0 * 2], screen_xy[i0 * 2 + 1], v0[2],
        vertex_uv[i0 * 2], vertex_uv[i0 * 2 + 1],
        screen_xy[i1 * 2], screen_xy[i1 * 2 + 1], v1[2],
        vertex_uv[i1 * 2], vertex_uv[i1 * 2 + 1],
        screen_xy[i2 * 2], screen_xy[i2 * 2 + 1], v2[2],
        vertex_uv[i2 * 2], vertex_uv[i2 * 2 + 1],
        texture);
  }

  font_draw(&font_default, backbuffer, 4, 4, 255, "spheremap_orb.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene spheremap_orb_scene = {setup, init, shutdown, render};
