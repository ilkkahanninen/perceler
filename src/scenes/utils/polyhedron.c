/*
 * polyhedron.c - Procedural Platonic solids with optional face extrusion.
 *
 * Vertex + face tables are defined in floats (normalised to circumradius 1);
 * the generator walks each face, fan-triangulates it, and emits Q8.8 ints
 * into temporary per-triangle-vertex arrays. Those arrays are then handed
 * to model_build_indexed() which dedupes vertices and produces the final
 * indexed Model. With POLYHEDRON_SMOOTH the dedup merges by position and
 * averages adjacent face normals into the shared vertex normal.
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

/* ------------------------------------------------------------------ */
/* Polyhedron data — vertices pre-normalised to circumradius 1.        */
/* ------------------------------------------------------------------ */

#define SQRT3  1.7320508075688772f
#define PHI    1.6180339887498949f
#define ICOSA_R 1.9021130325903071f

typedef struct
{
  const Vec3 *verts;
  float scale;
  const int *faces;
  int face_size;
  int num_faces;
} PolyData;

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

static const Vec3 cube_verts[8] = {
    {-1.0f, -1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f},
    {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f},
};
static const int cube_faces[6 * 4] = {
    4, 5, 6, 7,
    1, 0, 3, 2,
    7, 6, 2, 3,
    0, 1, 5, 4,
    5, 1, 2, 6,
    0, 4, 7, 3,
};
static const PolyData POLY_CUBE = {cube_verts, 1.0f / SQRT3, cube_faces, 4, 6};

static const Vec3 oct_verts[6] = {
    { 1.0f,  0.0f,  0.0f},
    {-1.0f,  0.0f,  0.0f},
    { 0.0f,  1.0f,  0.0f},
    { 0.0f, -1.0f,  0.0f},
    { 0.0f,  0.0f,  1.0f},
    { 0.0f,  0.0f, -1.0f},
};
static const int oct_faces[8 * 3] = {
    0, 2, 4,  0, 4, 3,  0, 5, 2,  0, 3, 5,
    1, 4, 2,  1, 3, 4,  1, 2, 5,  1, 5, 3,
};
static const PolyData POLY_OCT = {oct_verts, 1.0f, oct_faces, 3, 8};

static const Vec3 ico_verts[12] = {
    { 0.0f,  1.0f,   PHI},
    { 0.0f, -1.0f,   PHI},
    { 0.0f,  1.0f,  -PHI},
    { 0.0f, -1.0f,  -PHI},
    { 1.0f,   PHI,  0.0f},
    {-1.0f,   PHI,  0.0f},
    { 1.0f,  -PHI,  0.0f},
    {-1.0f,  -PHI,  0.0f},
    {  PHI,  0.0f,  1.0f},
    { -PHI,  0.0f,  1.0f},
    {  PHI,  0.0f, -1.0f},
    { -PHI,  0.0f, -1.0f},
};
static const int ico_faces[20 * 3] = {
    0,  1,  8,   0,  8,  4,   0,  4,  5,   0,  5,  9,   0,  9,  1,
    1,  6,  8,   8,  6, 10,   8, 10,  4,   4, 10,  2,   4,  2,  5,
    5,  2, 11,   5, 11,  9,   9, 11,  7,   9,  7,  1,   1,  7,  6,
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

typedef struct
{
  int u, v;
} Uv88;

typedef struct
{
  int *positions;     /* num_tri * 9 */
  int *uvs;           /* num_tri * 6 */
  int *face_normals;  /* num_tri * 3 */
  int *vertex_normals;/* num_tri * 9 — vn = fn for now; smoothing is
                       *               applied later by the indexer. */
} RawTris;

static void emit_tri(RawTris *r, int i, Vec3 a, Vec3 b, Vec3 c, Vec3 n,
                     Uv88 ua, Uv88 ub, Uv88 uc)
{
  int po = i * 9;
  int uo = i * 6;
  int no = i * 3;
  int vno = i * 9;
  int nx = FLOAT_TO_FP(n.x), ny = FLOAT_TO_FP(n.y), nz = FLOAT_TO_FP(n.z);
  int j;
  r->positions[po + 0] = FLOAT_TO_FP(a.x);
  r->positions[po + 1] = FLOAT_TO_FP(a.y);
  r->positions[po + 2] = FLOAT_TO_FP(a.z);
  r->positions[po + 3] = FLOAT_TO_FP(b.x);
  r->positions[po + 4] = FLOAT_TO_FP(b.y);
  r->positions[po + 5] = FLOAT_TO_FP(b.z);
  r->positions[po + 6] = FLOAT_TO_FP(c.x);
  r->positions[po + 7] = FLOAT_TO_FP(c.y);
  r->positions[po + 8] = FLOAT_TO_FP(c.z);
  r->uvs[uo + 0] = ua.u; r->uvs[uo + 1] = ua.v;
  r->uvs[uo + 2] = ub.u; r->uvs[uo + 3] = ub.v;
  r->uvs[uo + 4] = uc.u; r->uvs[uo + 5] = uc.v;
  r->face_normals[no + 0] = nx;
  r->face_normals[no + 1] = ny;
  r->face_normals[no + 2] = nz;
  /* Default vertex normals = face normal. POLYHEDRON_SMOOTH triggers
   * dedupe-by-position in model_build_indexed which then averages the
   * face normals across shared vertices. */
  for (j = 0; j < 3; j++)
  {
    r->vertex_normals[vno + j * 3 + 0] = nx;
    r->vertex_normals[vno + j * 3 + 1] = ny;
    r->vertex_normals[vno + j * 3 + 2] = nz;
  }
}

static Uv88 face_uv(int face_size, int idx)
{
  Uv88 r = {0, 0};
  if (face_size == 3)
  {
    if (idx == 1) { r.u = 256; r.v = 0; }
    else if (idx == 2) { r.u = 0; r.v = 256; }
  }
  else if (face_size == 4)
  {
    if (idx == 1) { r.u = 256; r.v = 0; }
    else if (idx == 2) { r.u = 256; r.v = 256; }
    else if (idx == 3) { r.u = 0; r.v = 256; }
  }
  return r;
}

#define POLY_MAX_FACE_VERTS 8

/* ------------------------------------------------------------------ */
/* Public: generate a Model                                            */
/* ------------------------------------------------------------------ */
Model *polyhedron_create(PolyhedronKind kind, int extrude_fp, int scale_fp,
                         unsigned flags)
{
  const PolyData *d = select_data(kind);
  Vec3 face[POLY_MAX_FACE_VERTS];
  Vec3 top[POLY_MAX_FACE_VERTS];
  int fs, extruding, tri_per_face, num_tri;
  float extrude, scale, inv_r;
  RawTris raw = {0, 0, 0, 0};
  Model *m = 0;
  int f, i, out;
  int dedupe;

  if (!d || d->face_size > POLY_MAX_FACE_VERTS)
    return 0;

  extrude = (float)extrude_fp / 256.0f;
  scale = (float)scale_fp / 256.0f;
  inv_r = d->scale;
  fs = d->face_size;
  extruding = (extrude_fp != 0);

  tri_per_face = extruding ? (3 * fs - 2) : (fs - 2);
  num_tri = d->num_faces * tri_per_face;

  raw.positions     = (int *)malloc((unsigned)num_tri * 9 * sizeof(int));
  raw.uvs           = (int *)malloc((unsigned)num_tri * 6 * sizeof(int));
  raw.face_normals  = (int *)malloc((unsigned)num_tri * 3 * sizeof(int));
  raw.vertex_normals= (int *)malloc((unsigned)num_tri * 9 * sizeof(int));
  if (!raw.positions || !raw.uvs || !raw.face_normals || !raw.vertex_normals)
    goto done;

  out = 0;
  for (f = 0; f < d->num_faces; f++)
  {
    const int *idx = &d->faces[f * fs];
    Vec3 centroid = {0.0f, 0.0f, 0.0f};
    Vec3 normal, offset;
    Uv88 uv_bl = {0, 256}, uv_br = {256, 256};
    Uv88 uv_tr = {256, 0},  uv_tl = {0, 0};

    for (i = 0; i < fs; i++)
      face[i] = v_scale(d->verts[idx[i]], inv_r);

    normal = v_normalize(v_cross(v_sub(face[1], face[0]),
                                 v_sub(face[2], face[0])));

    if (!extruding)
    {
      for (i = 1; i < fs - 1; i++)
        emit_tri(&raw, out++, face[0], face[i], face[i + 1], normal,
                 face_uv(fs, 0), face_uv(fs, i), face_uv(fs, i + 1));
      continue;
    }

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

    for (i = 1; i < fs - 1; i++)
      emit_tri(&raw, out++, top[0], top[i], top[i + 1], normal,
               face_uv(fs, 0), face_uv(fs, i), face_uv(fs, i + 1));

    for (i = 0; i < fs; i++)
    {
      int j = (i + 1) % fs;
      Vec3 b0 = face[i], b1 = face[j];
      Vec3 t0 = top[i], t1 = top[j];
      Vec3 wall_normal = v_normalize(v_cross(v_sub(b1, b0), v_sub(t0, b0)));

      emit_tri(&raw, out++, b0, b1, t1, wall_normal, uv_bl, uv_br, uv_tr);
      emit_tri(&raw, out++, b0, t1, t0, wall_normal, uv_bl, uv_tr, uv_tl);
    }
  }

  dedupe = (flags & POLYHEDRON_SMOOTH) ? 1 : 0;
  m = model_build_indexed(num_tri,
                          raw.positions, raw.uvs, raw.vertex_normals,
                          raw.face_normals, dedupe);

done:
  free(raw.positions);
  free(raw.uvs);
  free(raw.face_normals);
  free(raw.vertex_normals);
  return m;
}
