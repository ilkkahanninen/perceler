#ifndef POLYHEDRON_H
#define POLYHEDRON_H

#include "model.h"

/*
 * Procedural Platonic-solid generator with optional per-face extrusion.
 *
 * Each face of the chosen polyhedron is emitted as triangles matching the
 * Model format (Q8.8 positions, one normal per triangle, UVs zeroed).
 *
 * Circumradius (vertex distance from origin) is normalised to 1.0 so all
 * kinds share a common bounding sphere.
 *
 * Extrusion:
 *   `extrude == 0` → flat polyhedron, `scale` ignored.
 *   `extrude != 0` → every face is pushed outward along its normal by
 *     `extrude` units; its vertices are also scaled toward the face
 *     centroid by `scale`. Side walls connecting the original face edge
 *     to the extruded edge are generated so the mesh stays solid.
 *       `scale == INT_TO_FP(1)`   → flat prism
 *       `scale == INT_TO_FP(1)/2` → truncated pyramid (narrower on top)
 *       `scale == 0`              → pyramid (collapses to a point)
 *
 * Both `extrude` and `scale` are 8.8 fixed-point (see utils/math.h).
 */

typedef enum
{
  POLYHEDRON_TETRAHEDRON,
  POLYHEDRON_CUBE,
  POLYHEDRON_OCTAHEDRON,
  POLYHEDRON_ICOSAHEDRON
} PolyhedronKind;

/* Generate a Model; pair with model_free(). Returns NULL on allocation
 * failure or unknown kind. */
Model *polyhedron_create(PolyhedronKind kind, int extrude, int scale);

#endif
