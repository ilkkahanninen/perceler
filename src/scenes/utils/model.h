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
 * triangles the file is exactly  4 + N * 108  bytes:
 *
 *      Offset           Size       Contents
 *      ---------------  ---------  ---------------------------------------
 *      0                4          num_triangles  (N)
 *      4                N * 36     positions[]       (9 ints / triangle)
 *      4 + N*36         N * 24     uvs[]             (6 ints / triangle)
 *      4 + N*60         N * 12     face_normals[]    (3 ints / triangle)
 *      4 + N*72         N * 36     vertex_normals[]  (9 ints / triangle)
 *                                  = N * 108 total payload
 *
 * The four arrays are stored back-to-back in that order, each laid out
 * in triangle order.  There is no padding and no per-triangle header.
 *
 * ======================================================================
 * Per-triangle layout
 * ======================================================================
 *
 *   positions     [i*9 + 0..8]  =  x0,y0,z0, x1,y1,z1, x2,y2,z2
 *   uvs           [i*6 + 0..5]  =  u0,v0,    u1,v1,    u2,v2
 *   face_normals  [i*3 + 0..2]  =  fnx, fny, fnz       (one per triangle)
 *   vertex_normals[i*9 + 0..8]  =  vn0x,vn0y,vn0z, vn1x,vn1y,vn1z,
 *                                  vn2x,vn2y,vn2z       (one per vertex)
 *
 * Vertex order encodes winding (CCW = front-facing under the usual right-
 * handed convention).  Triangles are stored flat (no index buffer) so the
 * renderer can walk all four streams as parallel sequential reads —
 * vertices shared between triangles are duplicated on disk.
 *
 * Face normals enable cheap view-space backface culling (one dot product
 * per triangle).  Vertex normals enable Gouraud shading: averaged across
 * adjacent faces at shared positions for smooth shading, or kept equal
 * to the face normal for hard edges.  The split between the two is set
 * at export time via OBJ smoothing groups (`s`) or 3DS smoothing-group
 * bitmasks.
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
 * in 32-bit intermediates — i.e. 8.8 * 8.8 -> 16.16 — which is safe only
 * while each input stays within signed 16-bit range (about -128.0 to
 * +127.996 in real units).  Authoring models outside that range will
 * overflow downstream fixed-point math (see utils/math.h, sin8/cos8).
 *
 * Unit normals therefore encode as values in roughly [-256, +256]:
 * +1.0 -> 256, -1.0 -> -256.  UVs are typically in [0, 1] real -> [0, 256]
 * stored.
 *
 * ======================================================================
 * Producing .mdl files
 * ======================================================================
 *
 *   python3 tools/obj2model.py  input.obj output.mdl
 *       One mesh per .obj; polygons are fan-triangulated.  Honours the
 *       `vn`/`v/vt/vn` constructs and `s` smoothing groups.
 *
 *   python3 tools/3ds2model.py  input.3ds output_dir/
 *       One .mdl per mesh in the .3ds; files named
 *       <input_stem>_<mesh_name>.mdl.  Honours per-face smoothing-group
 *       bitmasks.
 *
 * ======================================================================
 * Runtime usage
 * ======================================================================
 *
 *   Model *mdl = model_load(ASSET_TEAPOT_MDL, MODEL_GOURAUD);
 *   // walk mdl->positions, mdl->face_normals, mdl->vertex_normals as
 *   // flat int arrays; mdl->num_triangles gives stride multipliers.
 *   model_free(mdl);
 *
 * Pass MODEL_LOAD_* flags to skip arrays you don't need — the file is
 * still read whole, but only the requested buffers are allocated.
 */

/* Load flags. Combine with bitwise OR. */
#define MODEL_LOAD_POSITIONS      0x01
#define MODEL_LOAD_UVS            0x02
#define MODEL_LOAD_FACE_NORMALS   0x04
#define MODEL_LOAD_VERTEX_NORMALS 0x08

/* Convenience combos. */
#define MODEL_WIREFRAME (MODEL_LOAD_POSITIONS | MODEL_LOAD_FACE_NORMALS)
#define MODEL_FLAT      (MODEL_LOAD_POSITIONS | MODEL_LOAD_FACE_NORMALS)
#define MODEL_GOURAUD   (MODEL_LOAD_POSITIONS | MODEL_LOAD_FACE_NORMALS | \
                         MODEL_LOAD_VERTEX_NORMALS)
#define MODEL_TEXTURED  (MODEL_LOAD_POSITIONS | MODEL_LOAD_UVS |          \
                         MODEL_LOAD_FACE_NORMALS)
#define MODEL_ALL       (MODEL_LOAD_POSITIONS | MODEL_LOAD_UVS |          \
                         MODEL_LOAD_FACE_NORMALS |                        \
                         MODEL_LOAD_VERTEX_NORMALS)

typedef struct
{
  int num_triangles;

  /* Each pointer is non-NULL only if the corresponding MODEL_LOAD_*
   * flag was passed to model_load().  Polyhedron-built models populate
   * all four. */
  int *positions;      /* num_triangles * 9 */
  int *uvs;            /* num_triangles * 6 */
  int *face_normals;   /* num_triangles * 3 */
  int *vertex_normals; /* num_triangles * 9 */
} Model;

/* Load a model from the packed data file. `flags` selects which arrays
 * to allocate and copy out.  Returns 0 on error. */
Model *model_load(Asset asset, unsigned flags);

/* Free a model returned by model_load() (or polyhedron_create()). */
void model_free(Model *mdl);

#endif
