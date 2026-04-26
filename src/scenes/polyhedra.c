/*
 * polyhedra.c - Rotating wireframe gallery of the four Platonic solids
 * exported by utils/polyhedron.h. Cycles through them one at a time,
 * each with a different (extrude, scale) configuration so the range of
 * the extrusion feature is visible.
 */

#include "polyhedra.h"

#include "utils/draw.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/model.h"
#include "utils/polyhedron.h"
#include "utils/render3d.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

static const Camera3D camera = {
    INT_TO_FP(4),    /* cam_z */
    FP_ONE >> 2,     /* near_z */
    INT_TO_FP(200),  /* proj_scale */
    160, 100         /* cx, cy */
};

#define NUM_SHAPES         4
#define FRAMES_PER_SHAPE 180 /* ~3 s at 60 fps */

typedef struct
{
  PolyhedronKind kind;
  const char *name;
  int extrude; /* Q8.8 */
  int scale;   /* Q8.8 */
} PolyConfig;

/* Each config picks an extrusion that shows off the polyhedron differently:
 *   tetra  — pyramids on each face (scale = 0.5, so tapered)
 *   cube   — flat slabs on each face (scale = 1.0, no taper)
 *   octa   — sharp spikes (scale = 0, each face collapses to a point)
 *   icosa  — subtle embossing (small extrusion, slight taper) */
static const PolyConfig SHAPES[NUM_SHAPES] = {
    {POLYHEDRON_TETRAHEDRON, "TETRAHEDRON", INT_TO_FP(1) / 3, INT_TO_FP(1) / 2},
    {POLYHEDRON_CUBE,        "CUBE",        INT_TO_FP(1) / 4, INT_TO_FP(1)    },
    {POLYHEDRON_OCTAHEDRON,  "OCTAHEDRON",  INT_TO_FP(1) / 2, 0                },
    {POLYHEDRON_ICOSAHEDRON, "ICOSAHEDRON", INT_TO_FP(1) / 8, INT_TO_FP(3) / 4},
};

static Model *models[NUM_SHAPES];
static int *transformed; /* sized for the largest model's vertex count */
static int *screen_xy;
static signed char *visible;
static int max_verts;

static void set_palette(void)
{
  int i;
  for (i = 0; i < 256; i++)
    vga_setpalette((unsigned char)i,
                   (unsigned char)(i >> 2),
                   (unsigned char)(i >> 2),
                   (unsigned char)(i >> 2));
}

static void polyhedra_setup(void)
{
  int i;
  max_verts = 0;
  for (i = 0; i < NUM_SHAPES; i++)
  {
    models[i] = polyhedron_create(SHAPES[i].kind, SHAPES[i].extrude,
                                  SHAPES[i].scale, 0);
    if (models[i] && models[i]->num_vertices > max_verts)
      max_verts = models[i]->num_vertices;
  }
  if (max_verts > 0)
  {
    transformed = (int *)malloc((unsigned)max_verts * 3 * sizeof(int));
    screen_xy = (int *)malloc((unsigned)max_verts * 2 * sizeof(int));
    visible = (signed char *)malloc((unsigned)max_verts);
  }
}

static void polyhedra_init(const RenderContext *ctx)
{
  (void)ctx;
  set_palette();
}

static void polyhedra_shutdown(void)
{
  int i;
  for (i = 0; i < NUM_SHAPES; i++)
  {
    if (models[i])
    {
      model_free(models[i]);
      models[i] = NULL;
    }
  }
  free(transformed);
  transformed = NULL;
  free(screen_xy);
  screen_xy = NULL;
  free(visible);
  visible = NULL;
  max_verts = 0;
}

static void polyhedra_render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned int frame = ctx->frame;
  int shape_idx;
  const Model *m;
  int num_tris, num_verts;
  const int *indices;
  int i;

  shape_idx = (int)((frame / FRAMES_PER_SHAPE) % NUM_SHAPES);
  m = models[shape_idx];

  memset(backbuffer, 0, VGA_SIZE);

  if (m && transformed)
  {
    num_tris = m->num_triangles;
    num_verts = m->num_vertices;
    indices = m->indices;
    transform_points(transformed, m->positions, num_verts,
                     (unsigned char)frame, (unsigned char)(frame >> 1),
                     camera.cam_z);
    project_points(&camera, transformed, num_verts, screen_xy, visible);

    for (i = 0; i < num_tris; i++)
    {
      int i0 = indices[i * 3 + 0];
      int i1 = indices[i * 3 + 1];
      int i2 = indices[i * 3 + 2];
      int sx0, sy0, sx1, sy1, sx2, sy2;

      if (!visible[i0] || !visible[i1] || !visible[i2])
        continue;

      sx0 = screen_xy[i0 * 2]; sy0 = screen_xy[i0 * 2 + 1];
      sx1 = screen_xy[i1 * 2]; sy1 = screen_xy[i1 * 2 + 1];
      sx2 = screen_xy[i2 * 2]; sy2 = screen_xy[i2 * 2 + 1];

      draw_line(backbuffer, sx0, sy0, sx1, sy1, 200);
      draw_line(backbuffer, sx1, sy1, sx2, sy2, 200);
      draw_line(backbuffer, sx2, sy2, sx0, sy0, 200);
    }
  }

  font_draw(&font_default, backbuffer, 4, 4, 255, SHAPES[shape_idx].name);
  font_draw(&font_default, backbuffer, 4, 188, 255, "polyhedra.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene polyhedra_scene = {
    polyhedra_setup,
    polyhedra_init,
    polyhedra_shutdown,
    polyhedra_render,
};
