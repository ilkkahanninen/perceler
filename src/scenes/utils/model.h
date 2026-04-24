#ifndef MODEL_H
#define MODEL_H

#include "../../assets.h"
#include "math.h"

/*
 * 3D model format (.mdl) — binary layout for integer-arithmetic rendering.
 *
 * ======================================================================
 * File layout
 * ======================================================================
 *
 * Every value is a little-endian 32-bit signed integer (int32_t). With N
 * triangles the file is exactly  4 + N * 72  bytes:
 *
 *      Offset        Size         Contents
 *      ------------  -----------  -----------------------------------------
 *      0             4            num_triangles  (N)
 *      4             N * 36       positions[]    (9 ints per triangle)
 *      4 + N*36      N * 24       uvs[]          (6 ints per triangle)
 *      4 + N*60      N * 12       normals[]      (3 ints per triangle)
 *                    = N*72 total payload
 *
 * The three arrays are stored back-to-back in that order, each laid out
 * in triangle order (all 9 position ints for triangle 0, then all 9 for
 * triangle 1, etc.).  There is no padding and no per-triangle header.
 *
 * ======================================================================
 * Per-triangle layout
 * ======================================================================
 *
 *   positions[i*9 + 0..8]  =  x0,y0,z0, x1,y1,z1, x2,y2,z2
 *   uvs      [i*6 + 0..5]  =  u0,v0,   u1,v1,   u2,v2
 *   normals  [i*3 + 0..2]  =  nx, ny, nz        (one normal per triangle)
 *
 * Vertex order encodes winding (CCW = front-facing under the usual right-
 * handed convention).  Triangles are stored flat (no index buffer) so the
 * renderer can walk positions/uvs/normals as three parallel sequential
 * streams — vertices shared between triangles are duplicated on disk.
 *
 * Exactly one normal is stored per triangle: the face normal.  This gives
 * flat shading; smooth/Gouraud shading is not representable in this
 * format.  Converters compute the normal as the unit-length cross product
 * of two edges, (v1 - v0) x (v2 - v0), rounded to 8.8 fixed-point.
 *
 * ======================================================================
 * 8.8 fixed-point encoding
 * ======================================================================
 *
 * All coordinates (positions, UVs, normals) are encoded as
 *
 *      stored_int = round(real_value * 256)
 *
 * so the low 8 bits are the fractional part and the upper bits are the
 * integer part.  Each stored value is decoded back as  stored_int / 256.0.
 *
 * Although the file stores these in 32-bit ints (for a loader that never
 * has to sign-extend), the renderer's 8.8 arithmetic is intended to run
 * in 32-bit intermediates — i.e. 8.8 * 8.8 → 16.16 — which is safe only
 * while each input stays within signed 16-bit range (about -128.0 to
 * +127.996 in real units).  Authoring models outside that range will
 * overflow downstream fixed-point math (see utils/math.h, sin8/cos8).
 *
 * Unit normals therefore encode as values in roughly [-256, +256]:
 * +1.0 → 256, -1.0 → -256.  UVs are typically in [0, 1] real → [0, 256]
 * stored.
 *
 * ======================================================================
 * Producing .mdl files
 * ======================================================================
 *
 *   python3 tools/obj2model.py  input.obj output.mdl
 *       One mesh per .obj; polygons are fan-triangulated.
 *
 *   python3 tools/3ds2model.py  input.3ds output_dir/
 *       One .mdl per mesh in the .3ds; files named
 *       <input_stem>_<mesh_name>.mdl.
 *
 * ======================================================================
 * Runtime usage
 * ======================================================================
 *
 *   Model *mdl = model_load(ASSET_TEAPOT_MDL);
 *   // walk mdl->positions / mdl->uvs / mdl->normals as flat int arrays;
 *   // mdl->num_triangles gives the stride multipliers (9 / 6 / 3).
 *   model_free(mdl);
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
