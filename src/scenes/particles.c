/*
 * Particle showcase: a fire-coloured fountain with a snare-driven
 * burst and an attractor orbiting the spawn point.
 *
 * Exercises the particle system's continuous emission, beat-driven
 * burst, gravity + drag, colour ramps, per-particle render size, and
 * a moving spring attractor that yanks the embers into a swirl.
 */

#include "particles.h"

#include "../assets.h"
#include "utils/fft.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/particles.h"
#include "utils/render3d.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

static const Camera3D camera = {
    INT_TO_FP(8),   /* cam_z */
    FP_ONE >> 2,    /* near_z */
    INT_TO_FP(200), /* proj_scale */
    160, 100        /* cx, cy */
};

static const ParticleForces forces = {
    0,                  /* gx */
    -INT_TO_FP(1) / 48, /* gy — pulls particles down (world +y is up) */
    0,                  /* gz */
    230                 /* drag — small per-frame damping */
};

#define POOL_SIZE 2000
#define FOUNTAIN_RATE 6  /* particles spawned per frame */
#define BURST_FLOOR 100  /* peaks below this are treated as silence */
#define BURST_COOLDOWN 4 /* min frames between bursts (~quarter beat at 162 BPM) */
#define BURST_COUNT 200

/* Attractor orbits the spawn point on a circle of this radius (Q8.8)
 * in the y = ATTR_HEIGHT plane, completing a revolution every 128
 * frames. */
#define ATTR_RADIUS INT_TO_FP(3)
#define ATTR_HEIGHT INT_TO_FP(10)
#define ATTR_STRENGTH (FP_ONE / 196) /* spring constant per frame */

static ParticleSystem *ps;
static FFTTrack *snare;
static OnsetDetector snare_onset;
static unsigned int rng;

static void set_palette(void)
{
  /* Fire ramp: 0..63 black->red, 64..127 red->yellow, 128..191
   * yellow->white, 192..255 sustained white. */
  int i;
  for (i = 0; i < 256; i++)
  {
    int r, g, b;
    if (i < 64)
    {
      r = i;
      g = 0;
      b = 0;
    }
    else if (i < 128)
    {
      r = 63;
      g = i - 64;
      b = 0;
    }
    else if (i < 192)
    {
      r = 63;
      g = 63;
      b = i - 128;
    }
    else
    {
      r = 63;
      g = 63;
      b = 63;
    }
    vga_setpalette((unsigned char)i,
                   (unsigned char)r, (unsigned char)g, (unsigned char)b);
  }
}

static void setup(void)
{
  ps = particles_create(POOL_SIZE);
  snare = fft_load(ASSET_SNARE_FFT);
}

static void init(const RenderContext *ctx)
{
  rng = 0xC0FFEE5Du;
  onset_init(&snare_onset, BURST_FLOOR, BURST_COOLDOWN);
  if (ps)
    particles_clear(ps);
  set_palette();
  memset(ctx->backbuffer, 0, VGA_SIZE);
}

static void shutdown(void)
{
  particles_free(ps);
  ps = NULL;
  fft_free(snare);
  snare = NULL;
}

static void render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned char ay = (unsigned char)(ctx->frame >> 1);
  unsigned char ax = 16; /* slight downward tilt */
  int e;

  /* Continuous fountain: a slow stream of small embers from the origin. */
  particles_emit(ps,
                 0, INT_TO_FP(-3), 0,        /* origin */
                 0, INT_TO_FP(1) * 3 / 8, 0, /* base velocity, +y */
                 INT_TO_FP(1) / 8,           /* spread (jitter) */
                 60, 110,                    /* lifetime in frames */
                 180,                        /* base colour: yellow */
                 -180,                       /* ramp toward black */
                 1,                          /* size: single pixel */
                 FOUNTAIN_RATE, &rng);

  /* Snare-driven burst: peak-locked onset detector fires once per
   * audible peak in the snare-band energy; secondary bumps in the
   * decay tail are filtered by the floor and cooldown. */
  e = (int)fft_at(snare, ctx->timeline_frame);
  if (onset_step(&snare_onset, e))
  {
    particles_emit(ps,
                   0, INT_TO_FP(-3), 0,
                   0, INT_TO_FP(1) / 4, 0,
                   INT_TO_FP(1) / 8,
                   40, 90,
                   220,  /* hot near-white */
                   -220, /* fade through fire colours */
                   2,    /* 2x2 blocks */
                   BURST_COUNT, &rng);
  }

  /* Orbiting attractor — circles the spawn point in the horizontal
   * plane, pulling embers into a swirl. Apply before update so the
   * resulting velocity goes through drag and integration this frame. */
  {
    unsigned char attr_a = (unsigned char)(ctx->frame * 2);
    int attr_x = FP_MUL(ATTR_RADIUS, cos8(attr_a));
    int attr_z = FP_MUL(ATTR_RADIUS, sin8(attr_a));
    particles_apply_attractor(ps, attr_x, ATTR_HEIGHT, attr_z,
                              ATTR_STRENGTH);
  }

  particles_update(ps, &forces);

  memset(backbuffer, e >> 3, VGA_SIZE);
  particles_draw(ps, &camera, ay, ax, backbuffer);

  font_draw(&font_default, backbuffer, 4, 4, 255, "particles.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene particles_scene = {setup, init, shutdown, render};
