#ifndef RENDER3D_H
#define RENDER3D_H

/*
 * Reusable 3D primitives shared by the model_viewer and polyhedra
 * scenes:
 *   - rotate / translate vertex buffers around Y then X
 *   - rotate direction vectors (normals) without translation
 *   - perspective-project a view-space point to screen coords
 *   - view-space backface test from a transformed normal
 *   - z-buffered flat-shaded triangle rasterizer
 *
 * All Q8.8 fixed-point math; angles are 8-bit (0..255 spans a circle).
 * Use sin8/cos8 from math.h to derive the angle bytes.
 */

typedef struct
{
  int cam_z;      /* Q8.8 — added to z after rotation */
  int near_z;     /* Q8.8 — points closer than this are clipped */
  int proj_scale; /* Q8.8 — perspective scale factor */
  int cx, cy;     /* screen-space center, integer pixels */
} Camera3D;

/* Rotate `count` Q8.8 vec3 points around Y then X by 8-bit angles, then
 * add `translate_z` to z. dst/src are interleaved (x,y,z, x,y,z, ...).
 * Suitable for transforming triangle vertex buffers (count = num_tris*3). */
void transform_points(int *dst, const int *src, int count,
                      unsigned char angle_y, unsigned char angle_x,
                      int translate_z);

/* Same Y-then-X rotation, no translation; for normals/directions. */
void transform_dirs(int *dst, const int *src, int count,
                    unsigned char angle_y, unsigned char angle_x);

/* Perspective-project (x,y,z) to (sx,sy). Returns 0 if z < cam->near_z. */
int project3d(const Camera3D *cam, int x, int y, int z, int *sx, int *sy);

/* View-space backface test. Camera at origin; n is the triangle's
 * outward normal and v is any vertex (both transformed).
 * Returns nonzero if the triangle is back-facing. */
int backface3d(const int *n, const int *v);

/* Z-buffered flat-shaded triangle rasterizer.
 * (x0,y0) etc. are screen-space integer coords; z values are the
 * triangle's pre-projection view-space z (Q8.8) used for 1/z depth.
 * Every covered pixel that wins the depth test takes `color`. */
void fill_triangle_flat(unsigned char *buf, unsigned short *zb,
                        int x0, int y0, int z0,
                        int x1, int y1, int z1,
                        int x2, int y2, int z2,
                        unsigned char color);

/* Z-buffered Gouraud-shaded triangle rasterizer.
 * Same parameters as fill_triangle_flat plus a per-vertex 0..255
 * intensity (i0/i1/i2) that is linearly interpolated across the
 * triangle and written as the pixel value. With a greyscale palette
 * this gives smooth shading; with a colour ramp it gives smooth
 * tinting between the three vertices. */
void fill_triangle_gouraud(unsigned char *buf, unsigned short *zb,
                           int x0, int y0, int z0, int i0,
                           int x1, int y1, int z1, int i1,
                           int x2, int y2, int z2, int i2);

#endif
