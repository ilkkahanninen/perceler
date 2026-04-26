/*
 * tube.c - Sweep-along-path tube + rope generators.
 *
 * The mesh is laid out as a grid of (num_points × sides) ring vertices
 * connected by quads, each quad split into two triangles. Vertex
 * positions, normals and UVs are computed at setup time in float, then
 * quantised to Q8.8 on emit.
 *
 * Frames are computed by parallel transport: at each path point i we
 * rotate the previous frame by the minimal rotation that aligns its
 * tangent with the new tangent. The initial frame's "up" reference is
 * the world axis least parallel to the first tangent.
 */

#include "tube.h"

#include "math.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Float vec3 helpers (init time only)                                 */
/* ------------------------------------------------------------------ */
typedef struct
{
  float x, y, z;
} Vec3;

static Vec3 v3(float x, float y, float z) { Vec3 r; r.x = x; r.y = y; r.z = z; return r; }
static Vec3 v_sub(Vec3 a, Vec3 b)         { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static Vec3 v_add(Vec3 a, Vec3 b)         { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static Vec3 v_scale(Vec3 a, float s)      { return v3(a.x * s, a.y * s, a.z * s); }
static float v_dot(Vec3 a, Vec3 b)        { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 v_cross(Vec3 a, Vec3 b)
{
  return v3(a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
}
static Vec3 v_normalize(Vec3 a)
{
  float len2 = v_dot(a, a);
  if (len2 > 1e-12f)
  {
    float inv = 1.0f / (float)sqrt((double)len2);
    return v_scale(a, inv);
  }
  return v3(0.0f, 0.0f, 0.0f);
}

/* Rodrigues: rotate v around unit-length axis k by angle. */
static Vec3 v_rotate(Vec3 v, Vec3 k, float angle)
{
  float c = (float)cos((double)angle);
  float s = (float)sin((double)angle);
  float kdv = v_dot(k, v);
  Vec3 kxv = v_cross(k, v);
  return v3(v.x * c + kxv.x * s + k.x * kdv * (1.0f - c),
            v.y * c + kxv.y * s + k.y * kdv * (1.0f - c),
            v.z * c + kxv.z * s + k.z * kdv * (1.0f - c));
}

static Vec3 fp_to_v3(const int *p)
{
  return v3((float)p[0] / 256.0f, (float)p[1] / 256.0f, (float)p[2] / 256.0f);
}

/* ------------------------------------------------------------------ */
/* Path tangents and parallel-transport frames                          */
/* ------------------------------------------------------------------ */
static Vec3 path_tangent(const int *path, int num_points, int i)
{
  Vec3 a, b;
  if (i == 0)
  {
    a = fp_to_v3(path + 0);
    b = fp_to_v3(path + 3);
  }
  else if (i == num_points - 1)
  {
    a = fp_to_v3(path + (i - 1) * 3);
    b = fp_to_v3(path + i * 3);
  }
  else
  {
    a = fp_to_v3(path + (i - 1) * 3);
    b = fp_to_v3(path + (i + 1) * 3);
  }
  return v_normalize(v_sub(b, a));
}

/* Pick an initial up-vector roughly perpendicular to the first tangent. */
static Vec3 initial_up(Vec3 tangent)
{
  if (fabs((double)tangent.y) < 0.9)
    return v3(0.0f, 1.0f, 0.0f);
  return v3(1.0f, 0.0f, 0.0f);
}

/* Compute parallel-transport frames along the whole path. `rights`
 * and `ups` must each hold `num_points` Vec3 entries. */
static void compute_frames(const int *path, int num_points,
                           Vec3 *rights, Vec3 *ups, Vec3 *tangents)
{
  Vec3 prev_tangent, t;
  Vec3 up_ref;
  int i;

  prev_tangent = path_tangent(path, num_points, 0);
  tangents[0] = prev_tangent;
  up_ref = initial_up(prev_tangent);
  rights[0] = v_normalize(v_cross(prev_tangent, up_ref));
  ups[0] = v_normalize(v_cross(rights[0], prev_tangent));

  for (i = 1; i < num_points; i++)
  {
    Vec3 axis;
    float axis_len2, dot_t, angle;

    t = path_tangent(path, num_points, i);
    tangents[i] = t;
    axis = v_cross(prev_tangent, t);
    axis_len2 = v_dot(axis, axis);

    if (axis_len2 < 1e-10f)
    {
      /* Tangent didn't change direction (or flipped 180°, which we
       * silently ignore — degenerate paths the caller can avoid). */
      rights[i] = rights[i - 1];
      ups[i] = ups[i - 1];
    }
    else
    {
      Vec3 axis_n = v_scale(axis, 1.0f / (float)sqrt((double)axis_len2));
      dot_t = v_dot(prev_tangent, t);
      if (dot_t > 1.0f) dot_t = 1.0f;
      else if (dot_t < -1.0f) dot_t = -1.0f;
      angle = (float)acos((double)dot_t);
      rights[i] = v_normalize(v_rotate(rights[i - 1], axis_n, angle));
      ups[i] = v_normalize(v_rotate(ups[i - 1], axis_n, angle));
    }
    prev_tangent = t;
  }
}

/* ------------------------------------------------------------------ */
/* Tube/rope mesh construction                                         */
/* ------------------------------------------------------------------ */

#define TWO_PI_F 6.28318530717958647692f

/* Cross-section radius for the rope variant: base radius modulated by
 * `strands` cosine bumps. Bump amplitude is hard-coded at 25% of the
 * base radius — pleasant for typical strand counts. */
#define ROPE_BUMP_AMOUNT 0.25f

/* Build the tube. `strands == 0` means smooth circle (no bumps);
 * `strands > 0` enables rope-style modulation and applies `turns`
 * full rotations of the cross-section across the whole path.
 * `turns` is Q8.8 (256 = one full rotation). */
static Model *build_tube(const int *path, int num_points,
                         int radius_fp, int sides,
                         int strands, int turns_fp, unsigned flags)
{
  Vec3 *rights = NULL, *ups = NULL, *tangents = NULL;
  Vec3 *ring_pos = NULL;   /* num_points * sides */
  Vec3 *ring_norm = NULL;  /* num_points * sides — radial outward */
  Model *m = NULL;
  int num_segments, num_tri;
  int i, s, ti;
  float radius, twist_total;

  if (num_points < 2 || sides < 3)
    return NULL;

  rights = (Vec3 *)malloc((unsigned)num_points * sizeof(Vec3));
  ups = (Vec3 *)malloc((unsigned)num_points * sizeof(Vec3));
  tangents = (Vec3 *)malloc((unsigned)num_points * sizeof(Vec3));
  ring_pos = (Vec3 *)malloc((unsigned)num_points * sides * sizeof(Vec3));
  ring_norm = (Vec3 *)malloc((unsigned)num_points * sides * sizeof(Vec3));
  if (!rights || !ups || !tangents || !ring_pos || !ring_norm)
    goto fail;

  compute_frames(path, num_points, rights, ups, tangents);

  radius = (float)radius_fp / 256.0f;
  twist_total = (float)turns_fp / 256.0f * TWO_PI_F;

  for (i = 0; i < num_points; i++)
  {
    Vec3 centre = fp_to_v3(path + i * 3);
    float twist_at_i = twist_total * (float)i / (float)(num_points - 1);

    for (s = 0; s < sides; s++)
    {
      float a = (float)s * TWO_PI_F / (float)sides + twist_at_i;
      float ca = (float)cos((double)a);
      float sa = (float)sin((double)a);
      float r = radius;
      Vec3 dir;

      if (strands > 0)
        r = radius * (1.0f + ROPE_BUMP_AMOUNT *
                      (float)cos((double)((float)strands * a)));

      dir = v_add(v_scale(rights[i], ca), v_scale(ups[i], sa));
      ring_pos[i * sides + s] = v_add(centre, v_scale(dir, r));
      /* Radial outward direction (used as vertex normal in smooth
       * mode). For the bumped rope this isn't the exact surface
       * normal — there's a tangential component proportional to the
       * radius derivative — but for rendering it reads as a smooth
       * round tube with grooves, which is what the variant is for. */
      ring_norm[i * sides + s] = v_normalize(dir);
    }
  }

  num_segments = num_points - 1;
  num_tri = num_segments * sides * 2;

  m = (Model *)malloc(sizeof(Model));
  if (!m)
    goto fail;
  m->num_triangles = num_tri;
  m->positions = (int *)malloc((unsigned)num_tri * 9 * sizeof(int));
  m->uvs = (int *)malloc((unsigned)num_tri * 6 * sizeof(int));
  m->face_normals = (int *)malloc((unsigned)num_tri * 3 * sizeof(int));
  m->vertex_normals = (int *)malloc((unsigned)num_tri * 9 * sizeof(int));
  if (!m->positions || !m->uvs || !m->face_normals || !m->vertex_normals)
  {
    model_free(m);
    m = NULL;
    goto fail;
  }

  ti = 0;
  for (i = 0; i < num_segments; i++)
  {
    int v_top = (int)((long)i * 256L / (long)num_segments);
    int v_bot = (int)((long)(i + 1) * 256L / (long)num_segments);
    for (s = 0; s < sides; s++)
    {
      int s_next = (s + 1) % sides;
      int u_l = (int)((long)s * 256L / (long)sides);
      int u_r = (int)((long)(s + 1) * 256L / (long)sides);
      Vec3 a = ring_pos[i * sides + s];
      Vec3 b = ring_pos[i * sides + s_next];
      Vec3 c = ring_pos[(i + 1) * sides + s_next];
      Vec3 d = ring_pos[(i + 1) * sides + s];
      Vec3 na = ring_norm[i * sides + s];
      Vec3 nb = ring_norm[i * sides + s_next];
      Vec3 nc = ring_norm[(i + 1) * sides + s_next];
      Vec3 nd = ring_norm[(i + 1) * sides + s];
      int t;

      /* Triangle 1: (a, d, c) — top-left, bottom-left, bottom-right.
       * Triangle 2: (a, c, b) — top-left, bottom-right, top-right.
       * CCW from outside given the right-handed frame. */
      for (t = 0; t < 2; t++)
      {
        Vec3 p0, p1, p2, vn0, vn1, vn2, edge1, edge2, fnv;
        int u0v0u, u0v0v, u1v1u, u1v1v, u2v2u, u2v2v;
        int po, uo, no, vno, j;

        if (t == 0)
        {
          p0 = a; p1 = d; p2 = c;
          vn0 = na; vn1 = nd; vn2 = nc;
          u0v0u = u_l; u0v0v = v_top;
          u1v1u = u_l; u1v1v = v_bot;
          u2v2u = u_r; u2v2v = v_bot;
        }
        else
        {
          p0 = a; p1 = c; p2 = b;
          vn0 = na; vn1 = nc; vn2 = nb;
          u0v0u = u_l; u0v0v = v_top;
          u1v1u = u_r; u1v1v = v_bot;
          u2v2u = u_r; u2v2v = v_top;
        }

        edge1 = v_sub(p1, p0);
        edge2 = v_sub(p2, p0);
        fnv = v_normalize(v_cross(edge1, edge2));

        po = ti * 9;
        uo = ti * 6;
        no = ti * 3;
        vno = ti * 9;
        m->positions[po + 0] = FLOAT_TO_FP(p0.x);
        m->positions[po + 1] = FLOAT_TO_FP(p0.y);
        m->positions[po + 2] = FLOAT_TO_FP(p0.z);
        m->positions[po + 3] = FLOAT_TO_FP(p1.x);
        m->positions[po + 4] = FLOAT_TO_FP(p1.y);
        m->positions[po + 5] = FLOAT_TO_FP(p1.z);
        m->positions[po + 6] = FLOAT_TO_FP(p2.x);
        m->positions[po + 7] = FLOAT_TO_FP(p2.y);
        m->positions[po + 8] = FLOAT_TO_FP(p2.z);
        m->uvs[uo + 0] = u0v0u; m->uvs[uo + 1] = u0v0v;
        m->uvs[uo + 2] = u1v1u; m->uvs[uo + 3] = u1v1v;
        m->uvs[uo + 4] = u2v2u; m->uvs[uo + 5] = u2v2v;
        m->face_normals[no + 0] = FLOAT_TO_FP(fnv.x);
        m->face_normals[no + 1] = FLOAT_TO_FP(fnv.y);
        m->face_normals[no + 2] = FLOAT_TO_FP(fnv.z);

        if (flags & POLYHEDRON_SMOOTH)
        {
          m->vertex_normals[vno + 0] = FLOAT_TO_FP(vn0.x);
          m->vertex_normals[vno + 1] = FLOAT_TO_FP(vn0.y);
          m->vertex_normals[vno + 2] = FLOAT_TO_FP(vn0.z);
          m->vertex_normals[vno + 3] = FLOAT_TO_FP(vn1.x);
          m->vertex_normals[vno + 4] = FLOAT_TO_FP(vn1.y);
          m->vertex_normals[vno + 5] = FLOAT_TO_FP(vn1.z);
          m->vertex_normals[vno + 6] = FLOAT_TO_FP(vn2.x);
          m->vertex_normals[vno + 7] = FLOAT_TO_FP(vn2.y);
          m->vertex_normals[vno + 8] = FLOAT_TO_FP(vn2.z);
        }
        else
        {
          int nx = FLOAT_TO_FP(fnv.x), ny = FLOAT_TO_FP(fnv.y), nz = FLOAT_TO_FP(fnv.z);
          for (j = 0; j < 3; j++)
          {
            m->vertex_normals[vno + j * 3 + 0] = nx;
            m->vertex_normals[vno + j * 3 + 1] = ny;
            m->vertex_normals[vno + j * 3 + 2] = nz;
          }
        }
        ti++;
      }
    }
  }

fail:
  free(rights);
  free(ups);
  free(tangents);
  free(ring_pos);
  free(ring_norm);
  return m;
}

Model *tube_create(const int *path, int num_points,
                   int radius, int sides, unsigned flags)
{
  return build_tube(path, num_points, radius, sides, 0, 0, flags);
}

Model *rope_create(const int *path, int num_points,
                   int radius, int strands, int turns, unsigned flags)
{
  int sides;
  if (strands < 1) strands = 1;
  sides = strands * 6;
  return build_tube(path, num_points, radius, sides, strands, turns, flags);
}
