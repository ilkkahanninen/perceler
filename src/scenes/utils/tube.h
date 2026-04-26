#ifndef TUBE_H
#define TUBE_H

#include "model.h"
#include "polyhedron.h"  /* for POLYHEDRON_SMOOTH */

/*
 * Sweep-along-path procedural tube generator.
 *
 * Both functions take a polyline `path` of `num_points * 3` Q8.8 ints
 * (interleaved x, y, z) and produce a closed-mesh Model with all four
 * arrays populated (positions, uvs, face_normals, vertex_normals).
 *
 * The cross-section is oriented along the path with a rotation-
 * minimizing frame (parallel transport): the frame at point i is the
 * minimal rotation of the frame at point i-1 that aligns its tangent
 * with the new tangent. No flipping at inflections, no failure when
 * the path aligns with the world up-axis.
 *
 * UVs walk 0..256 around the cross-section (one wrap) and 0..256
 * end-to-end along the path.
 *
 * `flags` accepts POLYHEDRON_SMOOTH from polyhedron.h. With smooth,
 * vertex normals point radially outward from the path centerline at
 * each ring, so a Gouraud or sphere-mapped renderer treats the tube
 * as a continuous round surface. Without smooth, every vertex carries
 * its triangle's face normal — a polygonal pipe look.
 */

/* Smooth tube with regular n-sided polygon cross-section. `sides`
 * must be at least 3; 8 is a smooth-looking circle approximation. */
Model *tube_create(const int *path, int num_points,
                   int radius, int sides, unsigned flags);

/* Knotted rope variant: the same swept tube, but the cross-section
 * has `strands` cosine-modulated bumps (3 = standard rope, 4 = chunky
 * rope) and rotates by `turns` full revolutions over the entire path
 * length, producing the spiraling-grooves rope appearance.
 *
 *   strands  number of bumps around the cross-section (>= 1; 3 typical)
 *   turns    Q8.8 total rotation across the path (256 = one full turn,
 *            negative reverses direction)
 *
 * The cross-section is internally tessellated into `strands * 6`
 * vertices to render the bumps smoothly. */
Model *rope_create(const int *path, int num_points,
                   int radius, int strands, int turns, unsigned flags);

#endif
