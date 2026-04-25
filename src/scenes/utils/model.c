/*
 * model.c - Binary model loader
 *
 * Layout: [num_triangles] [positions] [uvs] [face_normals] [vertex_normals]
 * All values are little-endian 32-bit signed integers.  See model.h.
 */

#include "model.h"

#include <data.h>
#include <stdlib.h>
#include <string.h>

Model *model_load(Asset asset, unsigned flags)
{
  unsigned char *buf;
  Model *mdl;
  int num_tri;
  int pos_size, uv_size, fnorm_size, vnorm_size;
  const int *src;
  int ok = 1;

  buf = (unsigned char *)data_read(asset);
  if (!buf)
    return 0;

  src = (const int *)buf;
  num_tri = src[0];

  pos_size = num_tri * 9 * (int)sizeof(int);
  uv_size = num_tri * 6 * (int)sizeof(int);
  fnorm_size = num_tri * 3 * (int)sizeof(int);
  vnorm_size = num_tri * 9 * (int)sizeof(int);

  mdl = (Model *)malloc(sizeof(Model));
  if (!mdl)
  {
    free(buf);
    return 0;
  }
  mdl->num_triangles = num_tri;
  mdl->positions = 0;
  mdl->uvs = 0;
  mdl->face_normals = 0;
  mdl->vertex_normals = 0;

  if (flags & MODEL_LOAD_POSITIONS)
  {
    mdl->positions = (int *)malloc(pos_size);
    if (!mdl->positions)
      ok = 0;
    else
      memcpy(mdl->positions, src + 1, pos_size);
  }
  if (ok && (flags & MODEL_LOAD_UVS))
  {
    mdl->uvs = (int *)malloc(uv_size);
    if (!mdl->uvs)
      ok = 0;
    else
      memcpy(mdl->uvs, src + 1 + num_tri * 9, uv_size);
  }
  if (ok && (flags & MODEL_LOAD_FACE_NORMALS))
  {
    mdl->face_normals = (int *)malloc(fnorm_size);
    if (!mdl->face_normals)
      ok = 0;
    else
      memcpy(mdl->face_normals, src + 1 + num_tri * 9 + num_tri * 6,
             fnorm_size);
  }
  if (ok && (flags & MODEL_LOAD_VERTEX_NORMALS))
  {
    mdl->vertex_normals = (int *)malloc(vnorm_size);
    if (!mdl->vertex_normals)
      ok = 0;
    else
      memcpy(mdl->vertex_normals,
             src + 1 + num_tri * 9 + num_tri * 6 + num_tri * 3, vnorm_size);
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
  free(mdl->face_normals);
  free(mdl->vertex_normals);
  free(mdl);
}
