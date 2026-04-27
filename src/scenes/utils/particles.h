#ifndef PARTICLES_H
#define PARTICLES_H

#include "render3d.h"

/*
 * Particle system: SoA pool, Q8.8 world-space coordinates, integer-only
 * update and draw. Shares the existing 3D pipeline (Camera3D plus
 * Y-then-X rotation), so particles obey the same camera as a scene's
 * 3D content.
 *
 * Lifecycle:
 *   ParticleSystem *ps = particles_create(N);
 *   ... per frame:
 *     particles_emit(ps, ...);          (one or more times)
 *     particles_update(ps, &forces);
 *     particles_draw(ps, &cam, ay, ax, backbuffer);
 *   particles_free(ps);
 */

typedef struct
{
  int capacity;
  int count;          /* live particle count */
  int next_search;    /* spawn-search hint */

  /* SoA per-slot data. life[i] <= 0 means the slot is free. */
  int *x, *y, *z;
  int *vx, *vy, *vz;
  int *life;
  int *life_max;
  int *ramp;
  unsigned char *base;
  unsigned char *size;
} ParticleSystem;

/* Constant-per-frame forces. `drag` is a Q8.8 multiplier applied to
 * velocity each frame: 256 = no damping, 240 ≈ -6%/frame, 0 = instant
 * stop. Gravity components are added to velocity each frame. */
typedef struct
{
  int gx, gy, gz;
  int drag;
} ParticleForces;

/* Allocate `capacity` slots. Returns NULL on failure. */
ParticleSystem *particles_create(int capacity);

/* Free a system created by particles_create(). Safe to pass NULL. */
void particles_free(ParticleSystem *ps);

/* Mark every slot as free without deallocating. Use in init() so the
 * scene starts empty after a re-entry. */
void particles_clear(ParticleSystem *ps);

/* Per-frame integration: decay life, apply drag and gravity, integrate
 * position. Slots whose life decays to 0 become free for the next
 * spawn. */
void particles_update(ParticleSystem *ps, const ParticleForces *forces);

/* Spawn `count` particles. Each gets:
 *
 *   position = (x, y, z) — Q8.8 world coords
 *   velocity = (vx, vy, vz) plus a per-axis random offset uniformly
 *              distributed in [-spread, +spread] (Q8.8). Narrow spread
 *              produces a pencil stream, wider spread a spray.
 *   life     = uniform random in [life_min, life_max] frames
 *   colour   = walks linearly from `base` to `base + ramp` across the
 *              lifetime; saturates at 0 / 255
 *   render   = `size` × `size` pixel block (1 = single pixel)
 *
 * `*seed` is the LCG state shared with rand8/16/32 from math.h —
 * advanced in place so callers stay deterministic. Spawning into a
 * full system silently drops the overflow. */
void particles_emit(ParticleSystem *ps,
                    int x, int y, int z,
                    int vx, int vy, int vz, int spread,
                    int life_min, int life_max,
                    unsigned char base, int ramp,
                    unsigned char size,
                    int count, unsigned int *seed);

/* Project and rasterize live particles. Each particle is rotated by
 * (angle_y, angle_x) using the same convention as transform_points,
 * translated by `cam->cam_z`, projected, and drawn as an N×N block.
 * Particles behind the near plane are skipped; blocks are bounds-
 * clipped to 320×200. */
void particles_draw(const ParticleSystem *ps,
                    const Camera3D *cam,
                    unsigned char angle_y, unsigned char angle_x,
                    unsigned char *backbuffer);

/* Add a spring-like force toward `(x, y, z)` to every live particle's
 * velocity. The per-particle force is `strength * (target - particle)`
 * in Q8.8 — positive `strength` attracts, negative repels, magnitude
 * grows linearly with distance.
 *
 * Multiple attractors compose by summing, so call once per attractor
 * per frame. Call before particles_update so the resulting velocity
 * is fed through drag and position integration this frame. */
void particles_apply_attractor(ParticleSystem *ps,
                               int x, int y, int z, int strength);

/* Bounce live particles off the infinite plane `n·p = d`, where the
 * plane normal `(nx, ny, nz)` is a Q8.8 unit vector and `d` is a Q8.8
 * world-space offset. Particles on the n-positive side of the plane
 * are unaffected; particles that have crossed to the negative side
 * have their velocity reflected and their position mirrored back out:
 *
 *     v' = v - (1 + r) * (v · n) * n
 *     p' = p - (1 + r) * dist    * n     (dist = n·p - d, < 0)
 *
 * `restitution` is Q8.8: `FP_ONE` is a perfectly elastic bounce (no
 * energy loss); `0` strips the normal component entirely, leaving
 * the tangential — particles slide along the plane.
 *
 * Multiple planes compose by calling once per plane. Call after
 * particles_update() so the reflection sees the post-integration
 * position. */
void particles_bounce_plane(ParticleSystem *ps,
                            int nx, int ny, int nz, int d,
                            int restitution);

#endif
