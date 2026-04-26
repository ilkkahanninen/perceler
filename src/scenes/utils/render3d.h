#ifndef RENDER3D_H
#define RENDER3D_H

#include "../../assets.h"
#include "palette.h"

/*
 * 3D rendering primitives:
 *   - rotate / translate vertex buffers around Y then X
 *   - rotate direction vectors (normals) without translation
 *   - perspective-project a view-space point to screen coords
 *   - view-space backface test from a transformed normal
 *   - z-buffered flat-, Gouraud- and texture-mapped rasterizers
 *
 * All coordinates use Q8.8 fixed-point. Angles are 8-bit (0..255 spans
 * a circle); use sin8/cos8 from math.h to derive them.
 */

typedef struct
{
  int cam_z;      /* Q8.8 — added to z after rotation */
  int near_z;     /* Q8.8 — points closer than this are clipped */
  int proj_scale; /* Q8.8 — perspective scale factor */
  int cx, cy;     /* screen-space centre, integer pixels */
} Camera3D;

/* Rotate `count` Q8.8 vec3 points around Y then X by 8-bit angles, then
 * add `translate_z` to z. dst/src are interleaved (x,y,z, x,y,z, ...). */
void transform_points(int *dst, const int *src, int count,
                      unsigned char angle_y, unsigned char angle_x,
                      int translate_z);

/* Y-then-X rotation without translation; for normals and other
 * direction vectors. */
void transform_dirs(int *dst, const int *src, int count,
                    unsigned char angle_y, unsigned char angle_x);

/* Perspective-project view-space (x, y, z) to screen-space (sx, sy).
 * Returns 0 if z < cam->near_z, 1 otherwise. */
int project3d(const Camera3D *cam, int x, int y, int z, int *sx, int *sy);

/* View-space backface test. Camera at origin; n is the triangle's
 * outward normal and v is any of its vertices (both transformed).
 * Returns nonzero if the triangle is back-facing. */
int backface3d(const int *n, const int *v);

/* Z-buffered flat-shaded triangle. (x0,y0) etc. are screen-space
 * integer coords; z values are the pre-projection view-space z (Q8.8)
 * used for 1/z depth. Every covered pixel that wins the depth test
 * is set to `color`. */
void fill_triangle_flat(unsigned char *buf, unsigned short *zb,
                        int x0, int y0, int z0,
                        int x1, int y1, int z1,
                        int x2, int y2, int z2,
                        unsigned char color);

/* Z-buffered Gouraud-shaded triangle. Same parameters as
 * fill_triangle_flat plus a per-vertex 0..255 intensity (i0/i1/i2)
 * linearly interpolated across the triangle and written as the pixel
 * value. */
void fill_triangle_gouraud(unsigned char *buf, unsigned short *zb,
                           int x0, int y0, int z0, int i0,
                           int x1, int y1, int z1, int i1,
                           int x2, int y2, int z2, int i2);

/*
 * Square 8-bit indexed texture for the textured rasterizers.
 *
 * Side length must be a power of two and at most 256, so the
 * rasterizer can wrap UVs with a single AND and address texels as
 * `pixels[(v << log2_size) + u]`.
 *
 * `palette` carries the source BMP's palette; apply it via
 * palette_apply() to upload the texture's colours to the DAC.
 */
typedef struct
{
  unsigned char *pixels; /* size * size bytes */
  int size;              /* 64, 128 or 256 */
  int log2_size;         /* 6, 7 or 8 */
  Palette palette;
} Texture;

/* Load an 8-bit indexed BMP as a texture. Returns 0 if the bitmap
 * is not a 64×64, 128×128 or 256×256 square. */
Texture *texture_load(Asset asset);

/* Free a texture returned by texture_load(). Safe to pass NULL. */
void texture_free(Texture *tex);

/* Z-buffered perspective-correct textured triangle. (x0,y0) etc. are
 * screen-space integer coords; z values are the pre-projection
 * view-space z (Q8.8); (u0,v0) etc. are Q8.8 texture coordinates
 * where 256 == one full texture wrap. */
void fill_triangle_textured(unsigned char *buf, unsigned short *zb,
                            int x0, int y0, int z0, int u0, int v0,
                            int x1, int y1, int z1, int u1, int v1,
                            int x2, int y2, int z2, int u2, int v2,
                            const Texture *tex);

/* Z-buffered perspective-correct textured triangle with Gouraud
 * shading. Same parameters as fill_triangle_textured plus a per-vertex
 * 0..255 intensity (i0/i1/i2). Per pixel the sampled texel and the
 * brightness level (intensity >> 2) index `cm` to produce the final
 * palette index. */
void fill_triangle_textured_gouraud(unsigned char *buf, unsigned short *zb,
                                    int x0, int y0, int z0,
                                    int u0, int v0, int i0,
                                    int x1, int y1, int z1,
                                    int u1, int v1, int i1,
                                    int x2, int y2, int z2,
                                    int u2, int v2, int i2,
                                    const Texture *tex,
                                    const Colormap *cm);

/*
 * Sphere-map a Q8.8 unit normal vector (camera-space) to Q8.8 texture
 * coordinates where 256 == one full wrap:
 *
 *   u = (nx + 1) / 2
 *   v = (1 - ny) / 2     (ny inverted so positive Y maps to the top
 *                         of the texture)
 *
 * `nz` is ignored. The caller feeds the resulting (u, v) into any of
 * the textured rasterizers; the texture itself is treated as a
 * sphere-projected reflection map (top-row = looking up, etc.).
 */
static inline void sphere_map_uv(int nx, int ny, int *out_u, int *out_v)
{
  *out_u = (nx >> 1) + 128;
  *out_v = 128 - (ny >> 1);
}

#endif
