/*
 * Sphere-mapped teapot rendered at half resolution.
 *
 * The teapot has ~6300 triangles, so the per-pixel rasterizer cost
 * dominates frame time. We render with halved camera parameters
 * (cx, cy, proj_scale) into the upper-left 160x100 region of a
 * VGA_WIDTH-stride work buffer, then pixel-double up to 320x200 via
 * vga_blit_2x_strided. The 3D rasterizers hard-code VGA_WIDTH as
 * their destination stride, hence the strided variant of the blit.
 */

#include "spheremap.h"

#include "../assets.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/model.h"
#include "utils/palette.h"
#include "utils/render3d.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

/* Camera parameters are halved versions of model_gouraud's so the
 * teapot lands at the same on-screen size after the 2x scale-up. */
static const Camera3D camera = {
    INT_TO_FP(6),   /* cam_z */
    FP_ONE >> 2,    /* near_z */
    INT_TO_FP(100), /* proj_scale (model_gouraud uses 200 at full res) */
    80, 50          /* cx, cy — half-resolution screen centre */
};

static Model *teapot;
static Texture *texture;
static unsigned char *work_buf;       /* VGA_WIDTH * VGA_HALF_HEIGHT bytes */
static unsigned short *zbuffer;       /* same shape as work_buf */
static int *transformed;              /* num_verts * 3 */
static int *transformed_fnorms;       /* num_tris  * 3 */
static int *transformed_vnorms;       /* num_verts * 3 */
static int *vertex_uv;                /* num_verts * 2 (sphere-mapped) */
static int *screen_xy;                /* num_verts * 2 */
static signed char *visible;          /* num_verts */
static int num_tris, num_verts;

#define WORK_SIZE (VGA_WIDTH * VGA_HALF_HEIGHT)  /* 320 * 100 = 32000 */

static void setup(void)
{
  teapot = model_load(ASSET_TEAPOT_MDL, MODEL_GOURAUD);
  texture = texture_load(ASSET_LANDSCAPE_BMP);
  num_tris = teapot->num_triangles;
  num_verts = teapot->num_vertices;
  work_buf = (unsigned char *)malloc(WORK_SIZE);
  zbuffer = (unsigned short *)malloc(WORK_SIZE * sizeof(unsigned short));
  transformed = (int *)malloc(num_verts * 3 * sizeof(int));
  transformed_fnorms = (int *)malloc(num_tris * 3 * sizeof(int));
  transformed_vnorms = (int *)malloc(num_verts * 3 * sizeof(int));
  vertex_uv = (int *)malloc(num_verts * 2 * sizeof(int));
  screen_xy = (int *)malloc(num_verts * 2 * sizeof(int));
  visible = (signed char *)malloc(num_verts);
}

static void init(const RenderContext *ctx)
{
  (void)ctx;
  palette_apply(&texture->palette);
}

static void shutdown(void)
{
  model_free(teapot);
  teapot = NULL;
  texture_free(texture);
  texture = NULL;
  free(work_buf);
  work_buf = NULL;
  free(zbuffer);
  zbuffer = NULL;
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
}

static void render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned char ay = (unsigned char)ctx->frame;
  unsigned char ax = (unsigned char)(ctx->frame >> 1);
  const int *indices = teapot->indices;
  int i;

  /* Clear the half-res work buffer and its z-buffer. */
  memset(work_buf, 0, WORK_SIZE);
  memset(zbuffer, 0, WORK_SIZE * sizeof(unsigned short));

  transform_points(transformed, teapot->positions, num_verts, ay, ax,
                   camera.cam_z);
  transform_dirs(transformed_fnorms, teapot->face_normals, num_tris, ay, ax);
  transform_dirs(transformed_vnorms, teapot->vertex_normals, num_verts,
                 ay, ax);
  project_points(&camera, transformed, num_verts, screen_xy, visible);
  for (i = 0; i < num_verts; i++)
    sphere_map_uv(transformed_vnorms[i * 3 + 0], transformed_vnorms[i * 3 + 1],
                  &vertex_uv[i * 2 + 0], &vertex_uv[i * 2 + 1]);

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

    fill_triangle_textured_affine(work_buf, zbuffer,
        screen_xy[i0 * 2], screen_xy[i0 * 2 + 1], v0[2],
        vertex_uv[i0 * 2], vertex_uv[i0 * 2 + 1],
        screen_xy[i1 * 2], screen_xy[i1 * 2 + 1], v1[2],
        vertex_uv[i1 * 2], vertex_uv[i1 * 2 + 1],
        screen_xy[i2 * 2], screen_xy[i2 * 2 + 1], v2[2],
        vertex_uv[i2 * 2], vertex_uv[i2 * 2 + 1],
        texture);
  }

  /* Pixel-double the upper-left 160x100 region into the backbuffer. */
  vga_blit_2x_strided(work_buf, VGA_WIDTH, backbuffer);

  /* Full-resolution overlay sits on top of the scaled image. */
  font_draw(&font_default, backbuffer, 4, 4, 255, "spheremap.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene spheremap_scene = {setup, init, shutdown, render};
