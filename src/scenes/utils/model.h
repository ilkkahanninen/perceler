#ifndef MODEL_H
#define MODEL_H

#include "../../assets.h"
#include "math.h"

/*
 * 3D model format for integer-arithmetic rendering.
 *
 * Uses 8.8 fixed-point for vertex positions, texture UV coordinates,
 * and face normals. All arithmetic stays within 32 bits.
 *
 * No index buffers — triangles store vertex data directly for
 * sequential access during rendering.
 *
 * Per triangle (flat arrays, tightly packed):
 *   - positions: 9 ints (x0,y0,z0, x1,y1,z1, x2,y2,z2) in 8.8 fp
 *   - uvs:       6 ints (u0,v0, u1,v1, u2,v2) in 8.8 fp
 *   - normals:   3 ints (nx, ny, nz) in 8.8 fp
 */

typedef struct
{
  int num_triangles;

  int *positions; /* num_triangles * 9: x0,y0,z0, x1,y1,z1, x2,y2,z2 (8.8 fp) */
  int *uvs;       /* num_triangles * 6: u0,v0, u1,v1, u2,v2 (8.8 fp) */
  int *normals;   /* num_triangles * 3: nx, ny, nz (8.8 fp) */
} Model;

/* Load a model from the packed data file. Returns 0 on error. */
Model *model_load(Asset asset);

/* Free a model returned by model_load(). */
void model_free(Model *mdl);

#endif
