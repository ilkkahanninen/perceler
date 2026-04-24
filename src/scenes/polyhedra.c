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

#include <stdlib.h>
#include <string.h>
#include <vga.h>

#define CAM_Z      INT_TO_FP(4)
#define NEAR_Z     (FP_ONE >> 2)
#define CX         160
#define CY         100
#define PROJ_SCALE INT_TO_FP(200)

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
static int *transformed; /* sized for the largest model */
static int max_tris;

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
  max_tris = 0;
  for (i = 0; i < NUM_SHAPES; i++)
  {
    models[i] = polyhedron_create(SHAPES[i].kind, SHAPES[i].extrude,
                                  SHAPES[i].scale);
    if (models[i] && models[i]->num_triangles > max_tris)
      max_tris = models[i]->num_triangles;
  }
  if (max_tris > 0)
    transformed = (int *)malloc((unsigned)max_tris * 9 * sizeof(int));
}

static void polyhedra_init(unsigned char *backbuffer)
{
  (void)backbuffer;
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
  max_tris = 0;
}

static int project(int x, int y, int z, int *sx, int *sy)
{
  int s;
  if (z < NEAR_Z)
    return 0;
  s = FP_DIV(PROJ_SCALE, z);
  *sx = CX + FP_TO_INT(FP_MUL(x, s));
  *sy = CY - FP_TO_INT(FP_MUL(y, s));
  return 1;
}

/* Rotate the model's vertices around Y then X (same pattern as
 * model_viewer) and push them forward so they sit in front of the
 * camera. Result is written to `transformed`. */
static void transform_model(const Model *m, unsigned int frame)
{
  unsigned char ay = (unsigned char)(frame);
  unsigned char ax = (unsigned char)(frame >> 1);
  int sin_y = sin8(ay), cos_y = cos8(ay);
  int sin_x = sin8(ax), cos_x = cos8(ax);
  const int *src = m->positions;
  int *dst = transformed;
  int i, num_verts = m->num_triangles * 3;

  for (i = 0; i < num_verts; i++, src += 3, dst += 3)
  {
    int x = src[0], y = src[1], z = src[2];
    int rx = FP_MUL(x, cos_y) + FP_MUL(z, sin_y);
    int ry = y;
    int rz = FP_MUL(-x, sin_y) + FP_MUL(z, cos_y);
    dst[0] = rx;
    dst[1] = FP_MUL(ry, cos_x) - FP_MUL(rz, sin_x);
    dst[2] = FP_MUL(ry, sin_x) + FP_MUL(rz, cos_x) + CAM_Z;
  }
}

static void polyhedra_render(unsigned char *backbuffer, unsigned int frame,
                             unsigned int timeline_frame)
{
  int shape_idx;
  const Model *m;
  int num_tris;
  int i;
  (void)timeline_frame;

  shape_idx = (int)((frame / FRAMES_PER_SHAPE) % NUM_SHAPES);
  m = models[shape_idx];

  memset(backbuffer, 0, VGA_SIZE);

  if (m && transformed)
  {
    num_tris = m->num_triangles;
    transform_model(m, frame);

    for (i = 0; i < num_tris; i++)
    {
      int *v = transformed + i * 9;
      int sx0, sy0, sx1, sy1, sx2, sy2;

      if (!project(v[0], v[1], v[2], &sx0, &sy0) ||
          !project(v[3], v[4], v[5], &sx1, &sy1) ||
          !project(v[6], v[7], v[8], &sx2, &sy2))
        continue;

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
