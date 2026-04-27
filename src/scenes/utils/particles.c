/*
 * particles.c - struct-of-arrays particle pool with Q8.8 coordinates.
 *
 * The update step is a flat scan over all slots. Slots with life <= 0
 * are skipped; each live slot has its velocity damped by drag, has
 * gravity added, and its position integrated.
 *
 * The draw step inlines a Y-then-X rotation followed by perspective
 * projection — same maths as transform_points + project3d but done
 * per particle to avoid an intermediate transformed-positions buffer.
 */

#include "particles.h"

#include "math.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

ParticleSystem *particles_create(int capacity)
{
  ParticleSystem *ps;
  if (capacity <= 0)
    return 0;
  ps = (ParticleSystem *)malloc(sizeof(ParticleSystem));
  if (!ps)
    return 0;
  ps->capacity = capacity;
  ps->count = 0;
  ps->next_search = 0;
  ps->x = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->y = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->z = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->vx = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->vy = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->vz = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->life = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->life_max = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->ramp = (int *)malloc((unsigned)capacity * sizeof(int));
  ps->base = (unsigned char *)malloc((unsigned)capacity);
  ps->size = (unsigned char *)malloc((unsigned)capacity);

  if (!ps->x || !ps->y || !ps->z || !ps->vx || !ps->vy || !ps->vz ||
      !ps->life || !ps->life_max || !ps->ramp || !ps->base || !ps->size)
  {
    particles_free(ps);
    return 0;
  }
  particles_clear(ps);
  return ps;
}

void particles_free(ParticleSystem *ps)
{
  if (!ps)
    return;
  free(ps->x);
  free(ps->y);
  free(ps->z);
  free(ps->vx);
  free(ps->vy);
  free(ps->vz);
  free(ps->life);
  free(ps->life_max);
  free(ps->ramp);
  free(ps->base);
  free(ps->size);
  free(ps);
}

void particles_clear(ParticleSystem *ps)
{
  if (!ps)
    return;
  memset(ps->life, 0, (unsigned)ps->capacity * sizeof(int));
  ps->count = 0;
  ps->next_search = 0;
}

void particles_update(ParticleSystem *ps, const ParticleForces *forces)
{
  int i;
  int gx = forces->gx, gy = forces->gy, gz = forces->gz;
  int drag = forces->drag;
  int has_drag = (drag != FP_ONE);

  for (i = 0; i < ps->capacity; i++)
  {
    if (ps->life[i] <= 0)
      continue;
    ps->life[i]--;
    if (ps->life[i] == 0)
    {
      ps->count--;
      continue;
    }
    if (has_drag)
    {
      ps->vx[i] = FP_MUL(ps->vx[i], drag);
      ps->vy[i] = FP_MUL(ps->vy[i], drag);
      ps->vz[i] = FP_MUL(ps->vz[i], drag);
    }
    ps->vx[i] += gx;
    ps->vy[i] += gy;
    ps->vz[i] += gz;
    ps->x[i] += ps->vx[i];
    ps->y[i] += ps->vy[i];
    ps->z[i] += ps->vz[i];
  }
}

/* Find next free slot, scanning from next_search. -1 if full. */
static int find_free_slot(ParticleSystem *ps)
{
  int i, cap = ps->capacity, start = ps->next_search;
  for (i = 0; i < cap; i++)
  {
    int idx = start + i;
    if (idx >= cap)
      idx -= cap;
    if (ps->life[idx] <= 0)
    {
      ps->next_search = (idx + 1 == cap) ? 0 : idx + 1;
      return idx;
    }
  }
  return -1;
}

void particles_emit(ParticleSystem *ps,
                    int x, int y, int z,
                    int vx, int vy, int vz, int spread,
                    int life_min, int life_max,
                    unsigned char base, int ramp,
                    unsigned char size,
                    int count, unsigned int *seed)
{
  int i;
  int life_range;

  if (size == 0)
    size = 1;
  if (life_min < 1)
    life_min = 1;
  if (life_max < life_min)
    life_max = life_min;
  life_range = life_max - life_min + 1;

  for (i = 0; i < count; i++)
  {
    int slot = find_free_slot(ps);
    int dx, dy, dz, life;

    if (slot < 0)
      return;

    /* Random per-axis offset uniformly in [-spread, +spread]. */
    if (spread > 0)
    {
      unsigned int span = (unsigned int)(spread * 2 + 1);
      dx = (int)(rand32(seed) % span) - spread;
      dy = (int)(rand32(seed) % span) - spread;
      dz = (int)(rand32(seed) % span) - spread;
    }
    else
    {
      dx = dy = dz = 0;
    }

    life = (life_range > 1)
               ? (life_min + (int)(rand32(seed) % (unsigned int)life_range))
               : life_min;

    ps->x[slot] = x;
    ps->y[slot] = y;
    ps->z[slot] = z;
    ps->vx[slot] = vx + dx;
    ps->vy[slot] = vy + dy;
    ps->vz[slot] = vz + dz;
    ps->life[slot] = life;
    ps->life_max[slot] = life;
    ps->ramp[slot] = ramp;
    ps->base[slot] = base;
    ps->size[slot] = size;
    ps->count++;
  }
}

void particles_apply_attractor(ParticleSystem *ps,
                               int x, int y, int z, int strength)
{
  int i;
  for (i = 0; i < ps->capacity; i++)
  {
    int dx, dy, dz;
    if (ps->life[i] <= 0)
      continue;
    dx = x - ps->x[i];
    dy = y - ps->y[i];
    dz = z - ps->z[i];
    ps->vx[i] += FP_MUL(dx, strength);
    ps->vy[i] += FP_MUL(dy, strength);
    ps->vz[i] += FP_MUL(dz, strength);
  }
}

void particles_bounce_plane(ParticleSystem *ps,
                            int nx, int ny, int nz, int d,
                            int restitution)
{
  int factor_scale = FP_ONE + restitution; /* (1 + e) in Q8.8 */
  int i;

  for (i = 0; i < ps->capacity; i++)
  {
    int dist, v_dot_n, factor;

    if (ps->life[i] <= 0)
      continue;

    /* Signed distance to the plane: n · p - d. */
    dist = FP_MUL(ps->x[i], nx) + FP_MUL(ps->y[i], ny) +
           FP_MUL(ps->z[i], nz) - d;
    if (dist >= 0)
      continue; /* still on the n-positive side */

    /* If the particle is already heading back toward the positive
     * side, skip — most likely the previous frame's bounce hasn't
     * yet integrated us off the plane. */
    v_dot_n = FP_MUL(ps->vx[i], nx) + FP_MUL(ps->vy[i], ny) +
              FP_MUL(ps->vz[i], nz);
    if (v_dot_n >= 0)
      continue;

    /* Reflect velocity: subtract (1 + e) * (v · n) along n. */
    factor = FP_MUL(factor_scale, v_dot_n);
    ps->vx[i] -= FP_MUL(factor, nx);
    ps->vy[i] -= FP_MUL(factor, ny);
    ps->vz[i] -= FP_MUL(factor, nz);

    /* Mirror position out of penetration by (1 + e) * dist along n.
     * dist is negative, so this moves the particle in the +n
     * direction by (1 + e) * |dist| — a perfectly elastic plane
     * (e = 1) sends it as far above as it had crossed below; e = 0
     * leaves it flush with the plane. */
    factor = FP_MUL(factor_scale, dist);
    ps->x[i] -= FP_MUL(factor, nx);
    ps->y[i] -= FP_MUL(factor, ny);
    ps->z[i] -= FP_MUL(factor, nz);
  }
}

void particles_draw(const ParticleSystem *ps,
                    const Camera3D *cam,
                    unsigned char angle_y, unsigned char angle_x,
                    unsigned char *backbuffer)
{
  int sin_y = sin8(angle_y), cos_y = cos8(angle_y);
  int sin_x = sin8(angle_x), cos_x = cos8(angle_x);
  int near_z = cam->near_z;
  int proj = cam->proj_scale;
  int cx_screen = cam->cx;
  int cy_screen = cam->cy;
  int cam_z = cam->cam_z;
  int i;

  for (i = 0; i < ps->capacity; i++)
  {
    int x, y, z, rx, rz, ry, ny, sz, s, sx, sy;
    int age, color, size, half_lo;
    int x0, y0, x1, y1, dx, dy;

    if (ps->life[i] <= 0)
      continue;

    /* Y-then-X rotation, plus the cam_z translation on the final z. */
    x = ps->x[i];
    y = ps->y[i];
    z = ps->z[i];
    rx = FP_MUL(x, cos_y) + FP_MUL(z, sin_y);
    ry = y;
    rz = FP_MUL(-x, sin_y) + FP_MUL(z, cos_y);
    ny = FP_MUL(ry, cos_x) - FP_MUL(rz, sin_x);
    sz = FP_MUL(ry, sin_x) + FP_MUL(rz, cos_x) + cam_z;
    if (sz < near_z)
      continue;

    s = FP_DIV(proj, sz);
    sx = cx_screen + FP_TO_INT(FP_MUL(rx, s));
    sy = cy_screen - FP_TO_INT(FP_MUL(ny, s));

    /* Colour walks from base to base + ramp across the lifetime. */
    age = ps->life_max[i] - ps->life[i];
    if (ps->life_max[i] > 0)
      color = (int)ps->base[i] +
              (int)(((long)ps->ramp[i] * age) / ps->life_max[i]);
    else
      color = (int)ps->base[i];
    if (color < 0)
      color = 0;
    else if (color > 255)
      color = 255;

    /* N×N block centred on (sx, sy), bounds-clipped to 320×200. */
    size = ps->size[i];
    half_lo = size >> 1;
    x0 = sx - half_lo;
    y0 = sy - half_lo;
    x1 = x0 + size;
    y1 = y0 + size;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VGA_WIDTH) x1 = VGA_WIDTH;
    if (y1 > VGA_HEIGHT) y1 = VGA_HEIGHT;

    for (dy = y0; dy < y1; dy++)
    {
      unsigned char *row = backbuffer + dy * VGA_WIDTH;
      for (dx = x0; dx < x1; dx++)
        row[dx] = (unsigned char)color;
    }
  }
}
