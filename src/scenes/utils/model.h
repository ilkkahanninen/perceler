#ifndef MODEL_H
#define MODEL_H

#include "../../assets.h"
#include "math.h"

/*
 * 3D model format (.mdl, "MDL2") — indexed mesh for integer-arithmetic
 * rendering.
 *
 * ======================================================================
 * In-memory layout
 * ======================================================================
 *
 *   num_vertices    V
 *   num_triangles   N
 *
 *   positions       V * 3 ints   (x,y,z per vertex)
 *   uvs             V * 2 ints   (u,v per vertex)
 *   vertex_normals  V * 3 ints   (nx,ny,nz per vertex)
 *   face_normals    N * 3 ints   (nx,ny,nz per triangle, NOT indexed)
 *   indices         N * 3 ints   (3 vertex indices per triangle)
 *
 * `face_normals` is per-triangle (used for backface culling, one dot
 * product per triangle). The remaining vertex attributes are stored
 * once per unique vertex and shared between triangles via `indices` —
 * a position+UV+normal tuple is one vertex; if any attribute differs
 * (UV seam, hard edge), the exporter splits it into two vertices.
 *
 * Triangle i is built from indices[i*3 + 0..2]; each index points into
 * positions[]/uvs[]/vertex_normals[]. Winding is CCW for front-facing
 * under the usual right-handed convention.
 *
 * ======================================================================
 * File layout (.mdl, magic "MDL2")
 * ======================================================================
 *
 * Every value after the magic is a little-endian 32-bit signed integer.
 *
 *      Offset                     Size        Contents
 *      -------------------------  ----------  ------------------------
 *      0                          4           magic = 'M','D','L','2'
 *      4                          4           num_vertices  (V)
 *      8                          4           num_triangles (N)
 *      12                         V * 12      positions
 *      12 + V*12                  V * 8       uvs
 *      12 + V*20                  V * 12      vertex_normals
 *      12 + V*32                  N * 12      face_normals
 *      12 + V*32 + N*12           N * 12      indices
 *
 * Total payload = 12 + V*32 + N*24 bytes.
 *
 * ======================================================================
 * 8.8 fixed-point encoding
 * ======================================================================
 *
 * All coordinates (positions, UVs, normals) are encoded as
 *
 *      stored_int = round(real_value * 256)
 *
 * Each input must stay within signed 16-bit range so 8.8 * 8.8 -> 16.16
 * multiplies do not overflow downstream. Unit normals encode as values
 * in roughly [-256, +256]: +1.0 -> 256. UVs in [0, 1] real -> [0, 256]
 * stored.
 *
 * ======================================================================
 * Producing .mdl files
 * ======================================================================
 *
 *   python3 tools/obj2model.py  input.obj output.mdl
 *       One mesh per .obj; polygons are fan-triangulated. Honours the
 *       `vn` / `v/vt/vn` constructs and `s` smoothing groups. Vertices
 *       are deduplicated by exact (position, uv, normal) tuple match.
 *
 *   python3 tools/3ds2model.py  input.3ds output_dir/
 *       One .mdl per mesh in the .3ds; honours per-face smoothing-group
 *       bitmasks. Same vertex deduplication as obj2model.
 *
 * ======================================================================
 * Runtime usage
 * ======================================================================
 *
 *   Model *mdl = model_load(ASSET_FOO_MDL, MODEL_GOURAUD);
 *   ... transform mdl->positions / vertex_normals as flat int arrays
 *       (mdl->num_vertices entries), then iterate triangles using
 *       mdl->indices to look up per-triangle vertices.
 *   model_free(mdl);
 *
 * Pass MODEL_LOAD_* flags to skip arrays the scene does not need — the
 * file is still read whole, but only the requested buffers are allocated.
 * indices are always loaded.
 */

/* Per-array load flags; combine with bitwise OR. */
#define MODEL_LOAD_POSITIONS      0x01
#define MODEL_LOAD_UVS            0x02
#define MODEL_LOAD_FACE_NORMALS   0x04
#define MODEL_LOAD_VERTEX_NORMALS 0x08

/* Common combinations. */
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
  int num_vertices;
  int num_triangles;

  /* Per-vertex arrays (V entries each). Each pointer is non-NULL only
   * if the corresponding MODEL_LOAD_* flag was passed to model_load();
   * the procedural generators (polyhedron_create, tube_create,
   * rope_create) always populate all three. */
  int *positions;      /* num_vertices * 3 */
  int *uvs;            /* num_vertices * 2 */
  int *vertex_normals; /* num_vertices * 3 */

  /* Per-triangle face normal — not indexed (each triangle gets its
   * own entry). NULL if MODEL_LOAD_FACE_NORMALS was not requested. */
  int *face_normals;   /* num_triangles * 3 */

  /* Triangle index buffer — 3 indices per triangle into the per-vertex
   * arrays. Always allocated (not gated by load flags). */
  int *indices;        /* num_triangles * 3 */
} Model;

/* Load a model from the packed data file. `flags` selects which per-vertex
 * and per-triangle arrays to allocate and copy out. `indices` is always
 * loaded. Returns 0 on error. */
Model *model_load(Asset asset, unsigned flags);

/* Free a model returned by model_load() or any procedural generator.
 * Safe to pass NULL. */
void model_free(Model *mdl);

/* Build an indexed Model from per-triangle-vertex flat arrays (the
 * "unindexed" form: each triangle owns its own three copies of every
 * attribute). Used by the procedural generators to dedupe their raw
 * output into shared vertices.
 *
 *   tri_positions      N * 9  (x,y,z per triangle vertex), or NULL
 *   tri_uvs            N * 6  (u,v per triangle vertex),   or NULL
 *   tri_vertex_normals N * 9, or NULL
 *   face_normals       N * 3 (one per triangle),           or NULL
 *
 * `dedupe_by_position`: if non-zero, vertices that share the same
 * (x,y,z) merge regardless of UV/normal differences. Vertex normals
 * across merged vertices are averaged and renormalised — used for
 * smooth-shaded polyhedra. Otherwise vertices must match on every
 * supplied attribute.
 *
 * Inputs are not modified or freed. Returns NULL on allocation failure
 * or invalid input. */
Model *model_build_indexed(int num_triangles,
                           const int *tri_positions,
                           const int *tri_uvs,
                           const int *tri_vertex_normals,
                           const int *face_normals,
                           int dedupe_by_position);

#endif
