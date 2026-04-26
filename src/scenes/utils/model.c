/*
 * model.c - Binary model loader and indexed-mesh builder.
 *
 * File layout (.mdl, magic "MDL2"):
 *   [magic][V][N][positions:V*3][uvs:V*2][vnormals:V*3]
 *   [face_normals:N*3][indices:N*3]
 *
 * All values after the magic are little-endian 32-bit signed integers.
 * See model.h for the canonical spec.
 */

#include "model.h"

#include <data.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MDL2_MAGIC 0x324C444D /* 'M','D','L','2' little-endian */

Model *model_load(Asset asset, unsigned flags)
{
  unsigned char *buf;
  Model *mdl;
  unsigned magic;
  int num_vertices, num_triangles;
  int pos_size, uv_size, vnorm_size, fnorm_size, idx_size;
  const int *src;
  size_t off_pos, off_uv, off_vnorm, off_fnorm, off_idx;
  int ok = 1;

  buf = (unsigned char *)data_read(asset);
  if (!buf)
    return 0;

  src = (const int *)buf;
  magic = (unsigned)src[0];
  if (magic != MDL2_MAGIC)
  {
    free(buf);
    return 0;
  }

  num_vertices = src[1];
  num_triangles = src[2];

  pos_size   = num_vertices * 3 * (int)sizeof(int);
  uv_size    = num_vertices * 2 * (int)sizeof(int);
  vnorm_size = num_vertices * 3 * (int)sizeof(int);
  fnorm_size = num_triangles * 3 * (int)sizeof(int);
  idx_size   = num_triangles * 3 * (int)sizeof(int);

  /* Element offsets (in ints) past the 3-int header. */
  off_pos   = 3;
  off_uv    = off_pos   + (size_t)num_vertices * 3;
  off_vnorm = off_uv    + (size_t)num_vertices * 2;
  off_fnorm = off_vnorm + (size_t)num_vertices * 3;
  off_idx   = off_fnorm + (size_t)num_triangles * 3;

  mdl = (Model *)malloc(sizeof(Model));
  if (!mdl)
  {
    free(buf);
    return 0;
  }
  mdl->num_vertices = num_vertices;
  mdl->num_triangles = num_triangles;
  mdl->positions = 0;
  mdl->uvs = 0;
  mdl->vertex_normals = 0;
  mdl->face_normals = 0;
  mdl->indices = 0;

  if (flags & MODEL_LOAD_POSITIONS)
  {
    mdl->positions = (int *)malloc(pos_size);
    if (!mdl->positions) ok = 0;
    else memcpy(mdl->positions, src + off_pos, pos_size);
  }
  if (ok && (flags & MODEL_LOAD_UVS))
  {
    mdl->uvs = (int *)malloc(uv_size);
    if (!mdl->uvs) ok = 0;
    else memcpy(mdl->uvs, src + off_uv, uv_size);
  }
  if (ok && (flags & MODEL_LOAD_VERTEX_NORMALS))
  {
    mdl->vertex_normals = (int *)malloc(vnorm_size);
    if (!mdl->vertex_normals) ok = 0;
    else memcpy(mdl->vertex_normals, src + off_vnorm, vnorm_size);
  }
  if (ok && (flags & MODEL_LOAD_FACE_NORMALS))
  {
    mdl->face_normals = (int *)malloc(fnorm_size);
    if (!mdl->face_normals) ok = 0;
    else memcpy(mdl->face_normals, src + off_fnorm, fnorm_size);
  }
  /* Indices are always loaded. */
  if (ok)
  {
    mdl->indices = (int *)malloc(idx_size);
    if (!mdl->indices) ok = 0;
    else memcpy(mdl->indices, src + off_idx, idx_size);
  }

  free(buf);

  if (!ok)
  {
    model_free(mdl);
    return 0;
  }
  return mdl;
}

void model_free(Model *mdl)
{
  if (!mdl)
    return;
  free(mdl->positions);
  free(mdl->uvs);
  free(mdl->vertex_normals);
  free(mdl->face_normals);
  free(mdl->indices);
  free(mdl);
}

/* ------------------------------------------------------------------ */
/* model_build_indexed: dedupe per-triangle-vertex tuples into a       */
/* shared-vertex Model.                                                */
/*                                                                     */
/* Linear-scan dedup; O(V * unique_V). Inputs are typically a few      */
/* hundred to a few thousand vertices — fast enough at setup time.     */
/* ------------------------------------------------------------------ */

static int tuple_match(const int *a, const int *b, int n)
{
  int i;
  for (i = 0; i < n; i++)
    if (a[i] != b[i])
      return 0;
  return 1;
}

Model *model_build_indexed(int num_triangles,
                           const int *tri_positions,
                           const int *tri_uvs,
                           const int *tri_vertex_normals,
                           const int *face_normals,
                           int dedupe_by_position)
{
  int total_vertices = num_triangles * 3;
  int *unique_pos = 0;
  int *unique_uv = 0;
  int *unique_vn = 0;
  int *vn_count = 0; /* # of merged vertices contributing to each normal */
  int *indices = 0;
  Model *m = 0;
  int unique_count = 0;
  int i;

  if (num_triangles < 0)
    return 0;
  if (num_triangles == 0)
  {
    /* Edge case: empty mesh. Return a valid but empty Model. */
    m = (Model *)malloc(sizeof(Model));
    if (!m) return 0;
    m->num_vertices = 0;
    m->num_triangles = 0;
    m->positions = 0;
    m->uvs = 0;
    m->vertex_normals = 0;
    m->face_normals = 0;
    m->indices = 0;
    return m;
  }

  /* Worst-case sizes — every input vertex is unique. */
  if (tri_positions)
    unique_pos = (int *)malloc((unsigned)total_vertices * 3 * sizeof(int));
  if (tri_uvs)
    unique_uv = (int *)malloc((unsigned)total_vertices * 2 * sizeof(int));
  if (tri_vertex_normals)
  {
    unique_vn = (int *)malloc((unsigned)total_vertices * 3 * sizeof(int));
    if (dedupe_by_position)
      vn_count = (int *)malloc((unsigned)total_vertices * sizeof(int));
  }
  indices = (int *)malloc((unsigned)num_triangles * 3 * sizeof(int));

  if ((tri_positions && !unique_pos) ||
      (tri_uvs && !unique_uv) ||
      (tri_vertex_normals && !unique_vn) ||
      (dedupe_by_position && tri_vertex_normals && !vn_count) ||
      !indices)
    goto fail;

  for (i = 0; i < total_vertices; i++)
  {
    int j;
    int found = -1;
    const int *p_in = tri_positions     ? tri_positions     + i * 3 : 0;
    const int *u_in = tri_uvs           ? tri_uvs           + i * 2 : 0;
    const int *n_in = tri_vertex_normals ? tri_vertex_normals + i * 3 : 0;

    for (j = 0; j < unique_count; j++)
    {
      int matched = 1;
      if (p_in && !tuple_match(unique_pos + j * 3, p_in, 3))
        matched = 0;
      if (matched && !dedupe_by_position)
      {
        if (u_in && !tuple_match(unique_uv + j * 2, u_in, 2))
          matched = 0;
        if (matched && n_in &&
            !tuple_match(unique_vn + j * 3, n_in, 3))
          matched = 0;
      }
      if (matched)
      {
        found = j;
        break;
      }
    }

    if (found < 0)
    {
      found = unique_count++;
      if (p_in)
      {
        unique_pos[found * 3 + 0] = p_in[0];
        unique_pos[found * 3 + 1] = p_in[1];
        unique_pos[found * 3 + 2] = p_in[2];
      }
      if (u_in)
      {
        unique_uv[found * 2 + 0] = u_in[0];
        unique_uv[found * 2 + 1] = u_in[1];
      }
      if (n_in)
      {
        unique_vn[found * 3 + 0] = n_in[0];
        unique_vn[found * 3 + 1] = n_in[1];
        unique_vn[found * 3 + 2] = n_in[2];
        if (vn_count) vn_count[found] = 1;
      }
    }
    else if (dedupe_by_position && n_in)
    {
      /* Accumulate normals so we can average across merged vertices. */
      unique_vn[found * 3 + 0] += n_in[0];
      unique_vn[found * 3 + 1] += n_in[1];
      unique_vn[found * 3 + 2] += n_in[2];
      vn_count[found]++;
    }

    indices[i] = found;
  }

  /* In dedupe-by-position mode, renormalise the accumulated vertex
   * normals so they're unit-length again. */
  if (dedupe_by_position && unique_vn)
  {
    for (i = 0; i < unique_count; i++)
    {
      float nx = (float)unique_vn[i * 3 + 0];
      float ny = (float)unique_vn[i * 3 + 1];
      float nz = (float)unique_vn[i * 3 + 2];
      float len = (float)sqrt((double)(nx * nx + ny * ny + nz * nz));
      if (len > 0.001f)
      {
        unique_vn[i * 3 + 0] = (int)(nx * 256.0f / len);
        unique_vn[i * 3 + 1] = (int)(ny * 256.0f / len);
        unique_vn[i * 3 + 2] = (int)(nz * 256.0f / len);
      }
    }
  }

  m = (Model *)malloc(sizeof(Model));
  if (!m)
    goto fail;
  m->num_vertices = unique_count;
  m->num_triangles = num_triangles;
  m->positions = 0;
  m->uvs = 0;
  m->vertex_normals = 0;
  m->face_normals = 0;
  m->indices = indices;
  indices = 0; /* ownership transferred */

  if (tri_positions)
  {
    int sz = unique_count * 3 * (int)sizeof(int);
    m->positions = (int *)malloc((unsigned)sz);
    if (!m->positions) goto fail;
    memcpy(m->positions, unique_pos, sz);
  }
  if (tri_uvs)
  {
    int sz = unique_count * 2 * (int)sizeof(int);
    m->uvs = (int *)malloc((unsigned)sz);
    if (!m->uvs) goto fail;
    memcpy(m->uvs, unique_uv, sz);
  }
  if (tri_vertex_normals)
  {
    int sz = unique_count * 3 * (int)sizeof(int);
    m->vertex_normals = (int *)malloc((unsigned)sz);
    if (!m->vertex_normals) goto fail;
    memcpy(m->vertex_normals, unique_vn, sz);
  }
  if (face_normals)
  {
    int sz = num_triangles * 3 * (int)sizeof(int);
    m->face_normals = (int *)malloc((unsigned)sz);
    if (!m->face_normals) goto fail;
    memcpy(m->face_normals, face_normals, sz);
  }

  free(unique_pos);
  free(unique_uv);
  free(unique_vn);
  free(vn_count);
  return m;

fail:
  free(unique_pos);
  free(unique_uv);
  free(unique_vn);
  free(vn_count);
  free(indices);
  if (m) model_free(m);
  return 0;
}
