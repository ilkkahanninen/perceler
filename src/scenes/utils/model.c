/*
 * model.c - Binary model loader
 *
 * Loads the flat binary format produced by obj2model.py.
 *
 * Layout: [num_triangles] [positions] [uvs] [normals]
 * All values are little-endian 32-bit signed integers.
 */

#include "model.h"

#include <data.h>
#include <stdlib.h>
#include <string.h>

Model *model_load(Asset asset)
{
  unsigned char *buf;
  Model *mdl;
  int num_tri;
  int pos_size, uv_size, norm_size;
  const int *src;

  buf = (unsigned char *)data_read(asset);
  if (!buf)
    return 0;

  src = (const int *)buf;
  num_tri = src[0];

  pos_size = num_tri * 9 * (int)sizeof(int);
  uv_size = num_tri * 6 * (int)sizeof(int);
  norm_size = num_tri * 3 * (int)sizeof(int);

  mdl = (Model *)malloc(sizeof(Model));
  if (!mdl)
  {
    free(buf);
    return 0;
  }

  mdl->num_triangles = num_tri;
  mdl->positions = (int *)malloc(pos_size);
  mdl->uvs = (int *)malloc(uv_size);
  mdl->normals = (int *)malloc(norm_size);

  if (!mdl->positions || !mdl->uvs || !mdl->normals)
  {
    free(mdl->positions);
    free(mdl->uvs);
    free(mdl->normals);
    free(mdl);
    free(buf);
    return 0;
  }

  memcpy(mdl->positions, src + 1, pos_size);
  memcpy(mdl->uvs, src + 1 + num_tri * 9, uv_size);
  memcpy(mdl->normals, src + 1 + num_tri * 9 + num_tri * 6, norm_size);

  free(buf);
  return mdl;
}

void model_free(Model *mdl)
{
  if (!mdl)
    return;
  free(mdl->positions);
  free(mdl->uvs);
  free(mdl->normals);
  free(mdl);
}
