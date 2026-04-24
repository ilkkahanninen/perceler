/*
 * polyhedron.c - Procedural Platonic solids with optional face extrusion.
 *
 * Vertex + face tables are defined in floats (normalised to circumradius 1);
 * the generator walks each face, fan-triangulates it, and emits Q8.8 ints
 * into a freshly-allocated Model.
 *
 * When extrusion is non-zero, each face is rebuilt as a prism:
 *   - The extruded "top" polygon, fan-triangulated.
 *   - N side-wall quads (2 triangles each) bridging the original face
 *     boundary to the top boundary.
 *   - The original (bottom) face is NOT emitted — it sits inside the
 *     closed shell next to its neighbours.
 */

#include "polyhedron.h"

#include "math.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Small float vec3 helpers (init-time only)                          */
/* ------------------------------------------------------------------ */
typedef struct
{
  float x, y, z;
} Vec3;

static Vec3 v_sub(Vec3 a, Vec3 b)
{
  Vec3 r;
  r.x = a.x - b.x;
  r.y = a.y - b.y;
  r.z = a.z - b.z;
  return r;
}

static Vec3 v_add(Vec3 a, Vec3 b)
{
  Vec3 r;
  r.x = a.x + b.x;
  r.y = a.y + b.y;
  r.z = a.z + b.z;
  return r;
}

static Vec3 v_scale(Vec3 a, float s)
{
  Vec3 r;
  r.x = a.x * s;
  r.y = a.y * s;
  r.z = a.z * s;
  return r;
}

static Vec3 v_cross(Vec3 a, Vec3 b)
{
  Vec3 r;
  r.x = a.y * b.z - a.z * b.y;
  r.y = a.z * b.x - a.x * b.z;
  r.z = a.x * b.y - a.y * b.x;
  return r;
}

static Vec3 v_normalize(Vec3 a)
{
  float len2 = a.x * a.x + a.y * a.y + a.z * a.z;
  Vec3 r = {0.0f, 0.0f, 0.0f};
  if (len2 > 1e-12f)
  {
    float inv = 1.0f / (float)sqrt((double)len2);
    r.x = a.x * inv;
    r.y = a.y * inv;
    r.z = a.z * inv;
  }
  return r;
}

/* Round-to-nearest float → Q8.8. */
static int q88(float x)
{
  if (x >= 0.0f)
    return (int)(x * 256.0f + 0.5f);
  return -(int)(-x * 256.0f + 0.5f);
}

/* ------------------------------------------------------------------ */
/* Polyhedron data — vertices pre-normalised to circumradius 1.        */
/* ------------------------------------------------------------------ */

/* Circumradius of raw vertex lists (used once at init; the tables are
 * then interpreted as-is and scaled by 1/R on read). */
#define SQRT3  1.7320508075688772f
#define PHI    1.6180339887498949f
/* Icosahedron circumradius for vertices (0,±1,±φ) etc. is sqrt(1+φ²). */
#define ICOSA_R 1.9021130325903071f

typedef struct
{
  const Vec3 *verts;  /* raw vertex positions */
  float scale;        /* 1/circumradius — applied to make radius 1 */
  const int *faces;   /* flat array, face_size indices per face */
  int face_size;
  int num_faces;
} PolyData;

/* --- Tetrahedron (circumradius √3) --- */
static const Vec3 tet_verts[4] = {
    { 1.0f,  1.0f,  1.0f},
    {-1.0f, -1.0f,  1.0f},
    {-1.0f,  1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
};
static const int tet_faces[4 * 3] = {
    0, 1, 2,
    0, 2, 3,
    0, 3, 1,
    1, 3, 2,
};
static const PolyData POLY_TET = {tet_verts, 1.0f / SQRT3, tet_faces, 3, 4};

/* --- Cube (circumradius √3) --- */
static const Vec3 cube_verts[8] = {
    {-1.0f, -1.0f, -1.0f}, /* 0 */
    { 1.0f, -1.0f, -1.0f}, /* 1 */
    { 1.0f,  1.0f, -1.0f}, /* 2 */
    {-1.0f,  1.0f, -1.0f}, /* 3 */
    {-1.0f, -1.0f,  1.0f}, /* 4 */
    { 1.0f, -1.0f,  1.0f}, /* 5 */
    { 1.0f,  1.0f,  1.0f}, /* 6 */
    {-1.0f,  1.0f,  1.0f}, /* 7 */
};
/* Faces in CCW order when viewed from outside. */
static const int cube_faces[6 * 4] = {
    4, 5, 6, 7, /* +z front */
    1, 0, 3, 2, /* -z back  */
    7, 6, 2, 3, /* +y top   */
    0, 1, 5, 4, /* -y bottom*/
    5, 1, 2, 6, /* +x right */
    0, 4, 7, 3, /* -x left  */
};
static const PolyData POLY_CUBE = {cube_verts, 1.0f / SQRT3, cube_faces, 4, 6};

/* --- Octahedron (circumradius 1) --- */
static const Vec3 oct_verts[6] = {
    { 1.0f,  0.0f,  0.0f}, /* 0 +x */
    {-1.0f,  0.0f,  0.0f}, /* 1 -x */
    { 0.0f,  1.0f,  0.0f}, /* 2 +y */
    { 0.0f, -1.0f,  0.0f}, /* 3 -y */
    { 0.0f,  0.0f,  1.0f}, /* 4 +z */
    { 0.0f,  0.0f, -1.0f}, /* 5 -z */
};
static const int oct_faces[8 * 3] = {
    0, 2, 4,  0, 4, 3,  0, 5, 2,  0, 3, 5,
    1, 4, 2,  1, 3, 4,  1, 2, 5,  1, 5, 3,
};
static const PolyData POLY_OCT = {oct_verts, 1.0f, oct_faces, 3, 8};

/* --- Icosahedron (circumradius √(1+φ²)) --- */
static const Vec3 ico_verts[12] = {
    { 0.0f,  1.0f,   PHI},  /* 0  */
    { 0.0f, -1.0f,   PHI},  /* 1  */
    { 0.0f,  1.0f,  -PHI},  /* 2  */
    { 0.0f, -1.0f,  -PHI},  /* 3  */
    { 1.0f,   PHI,  0.0f},  /* 4  */
    {-1.0f,   PHI,  0.0f},  /* 5  */
    { 1.0f,  -PHI,  0.0f},  /* 6  */
    {-1.0f,  -PHI,  0.0f},  /* 7  */
    {  PHI,  0.0f,  1.0f},  /* 8  */
    { -PHI,  0.0f,  1.0f},  /* 9  */
    {  PHI,  0.0f, -1.0f},  /* 10 */
    { -PHI,  0.0f, -1.0f},  /* 11 */
};
static const int ico_faces[20 * 3] = {
    /* top cap around V0 */
    0,  1,  8,   0,  8,  4,   0,  4,  5,   0,  5,  9,   0,  9,  1,
    /* middle ring */
    1,  6,  8,   8,  6, 10,   8, 10,  4,   4, 10,  2,   4,  2,  5,
    5,  2, 11,   5, 11,  9,   9, 11,  7,   9,  7,  1,   1,  7,  6,
    /* bottom cap around V3 */
    3,  2, 10,   3, 10,  6,   3,  6,  7,   3,  7, 11,   3, 11,  2,
};
static const PolyData POLY_ICO = {ico_verts, 1.0f / ICOSA_R, ico_faces, 3, 20};

static const PolyData *select_data(PolyhedronKind kind)
{
  switch (kind)
  {
  case POLYHEDRON_TETRAHEDRON: return &POLY_TET;
  case POLYHEDRON_CUBE:        return &POLY_CUBE;
  case POLYHEDRON_OCTAHEDRON:  return &POLY_OCT;
  case POLYHEDRON_ICOSAHEDRON: return &POLY_ICO;
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* Triangle emitter                                                    */
/* ------------------------------------------------------------------ */
static void emit_tri(Model *m, int i, Vec3 a, Vec3 b, Vec3 c, Vec3 n)
{
  int po = i * 9;
  int no = i * 3;
  m->positions[po + 0] = q88(a.x);
  m->positions[po + 1] = q88(a.y);
  m->positions[po + 2] = q88(a.z);
  m->positions[po + 3] = q88(b.x);
  m->positions[po + 4] = q88(b.y);
  m->positions[po + 5] = q88(b.z);
  m->positions[po + 6] = q88(c.x);
  m->positions[po + 7] = q88(c.y);
  m->positions[po + 8] = q88(c.z);
  m->normals[no + 0] = q88(n.x);
  m->normals[no + 1] = q88(n.y);
  m->normals[no + 2] = q88(n.z);
}

/* Biggest face any supported polyhedron has (cube = 4). Keep the stack
 * buffers a touch larger in case future additions need pentagons. */
#define POLY_MAX_FACE_VERTS 8

/* ------------------------------------------------------------------ */
/* Public: generate a Model                                            */
/* ------------------------------------------------------------------ */
Model *polyhedron_create(PolyhedronKind kind, int extrude_fp, int scale_fp)
{
  const PolyData *d = select_data(kind);
  Vec3 face[POLY_MAX_FACE_VERTS];
  Vec3 top[POLY_MAX_FACE_VERTS];
  int fs, extruding, tri_per_face, num_tri;
  float extrude, scale, inv_r;
  Model *m;
  int f, i, out;

  if (!d || d->face_size > POLY_MAX_FACE_VERTS)
    return 0;

  extrude = (float)extrude_fp / 256.0f;
  scale = (float)scale_fp / 256.0f;
  inv_r = d->scale;
  fs = d->face_size;
  extruding = (extrude_fp != 0);

  tri_per_face = extruding ? (3 * fs - 2) : (fs - 2);
  num_tri = d->num_faces * tri_per_face;

  m = (Model *)malloc(sizeof(Model));
  if (!m)
    return 0;
  m->num_triangles = num_tri;
  m->positions = (int *)malloc((unsigned)num_tri * 9 * sizeof(int));
  m->uvs = (int *)malloc((unsigned)num_tri * 6 * sizeof(int));
  m->normals = (int *)malloc((unsigned)num_tri * 3 * sizeof(int));
  if (!m->positions || !m->uvs || !m->normals)
  {
    free(m->positions);
    free(m->uvs);
    free(m->normals);
    free(m);
    return 0;
  }
  memset(m->uvs, 0, (unsigned)num_tri * 6 * sizeof(int));

  out = 0;
  for (f = 0; f < d->num_faces; f++)
  {
    const int *idx = &d->faces[f * fs];
    Vec3 centroid = {0.0f, 0.0f, 0.0f};
    Vec3 normal, offset;

    /* Collect face vertices (pre-scaled to unit circumradius). */
    for (i = 0; i < fs; i++)
      face[i] = v_scale(d->verts[idx[i]], inv_r);

    /* Face normal from the first three vertices (planar assumption). */
    normal = v_normalize(v_cross(v_sub(face[1], face[0]),
                                 v_sub(face[2], face[0])));

    if (!extruding)
    {
      for (i = 1; i < fs - 1; i++)
        emit_tri(m, out++, face[0], face[i], face[i + 1], normal);
      continue;
    }

    /* Build the extruded top face. */
    for (i = 0; i < fs; i++)
    {
      centroid.x += face[i].x;
      centroid.y += face[i].y;
      centroid.z += face[i].z;
    }
    centroid.x /= (float)fs;
    centroid.y /= (float)fs;
    centroid.z /= (float)fs;
    offset = v_scale(normal, extrude);

    for (i = 0; i < fs; i++)
    {
      Vec3 rel = v_sub(face[i], centroid);
      Vec3 pulled = v_scale(rel, scale);
      top[i] = v_add(v_add(centroid, pulled), offset);
    }

    /* Top face (fan-triangulated). */
    for (i = 1; i < fs - 1; i++)
      emit_tri(m, out++, top[0], top[i], top[i + 1], normal);

    /* Side walls: CCW from outside is bottom[i] -> bottom[j] -> top[j]
     * -> top[i]. Split into two triangles. Wall normal from (b1-b0) ×
     * (t0-b0). When scale→0 the quad degenerates toward the top apex;
     * emit_tri still works (normal might be near-zero if everything
     * coincides, but that's an edge case the caller can avoid). */
    for (i = 0; i < fs; i++)
    {
      int j = (i + 1) % fs;
      Vec3 b0 = face[i], b1 = face[j];
      Vec3 t0 = top[i], t1 = top[j];
      Vec3 wall_normal = v_normalize(v_cross(v_sub(b1, b0), v_sub(t0, b0)));

      emit_tri(m, out++, b0, b1, t1, wall_normal);
      emit_tri(m, out++, b0, t1, t0, wall_normal);
    }
  }

  return m;
}
