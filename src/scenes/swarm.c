/*
 * Swarm: particles ricocheting inside a cubic chamber.
 *
 * Six bounce planes form an axis-aligned box centred on the origin.
 * A few particles emit at the centre each frame with random velocity;
 * snare hits inject a larger burst. Drag is light enough that
 * particles bounce many times before settling. The chamber's
 * wireframe is drawn each frame so the bounce surfaces are visible.
 */

#include "swarm.h"

#include "../assets.h"
#include "utils/draw.h"
#include "utils/fft.h"
#include "utils/font.h"
#include "utils/math.h"
#include "utils/particles.h"
#include "utils/render3d.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>

static const Camera3D camera = {
    INT_TO_FP(9),   /* cam_z */
    FP_ONE >> 2,    /* near_z */
    INT_TO_FP(160), /* proj_scale */
    160, 100        /* cx, cy */
};

static const ParticleForces forces = {
    0,                   /* gx */
    -INT_TO_FP(1) / 128, /* gy — very weak gravity */
    0,                   /* gz */
    253                  /* drag — slow energy bleed */
};

#define POOL_SIZE 1500
#define BOX_HALF INT_TO_FP(3)
#define BOX_BOUNCE (FP_ONE * 7 / 10) /* 0.7 restitution */
#define EMIT_RATE 1
#define BURST_COUNT 100
#define BURST_FLOOR 100
#define BURST_COOLDOWN 50
#define WIRE_COLOR 64

/* Box corners: bit 0 = x, bit 1 = y, bit 2 = z. */
static const int box_corners[8 * 3] = {
    -BOX_HALF,
    -BOX_HALF,
    -BOX_HALF, /* 0: --- */
    BOX_HALF,
    -BOX_HALF,
    -BOX_HALF, /* 1: +-- */
    -BOX_HALF,
    BOX_HALF,
    -BOX_HALF, /* 2: -+- */
    BOX_HALF,
    BOX_HALF,
    -BOX_HALF, /* 3: ++- */
    -BOX_HALF,
    -BOX_HALF,
    BOX_HALF, /* 4: --+ */
    BOX_HALF,
    -BOX_HALF,
    BOX_HALF, /* 5: +-+ */
    -BOX_HALF,
    BOX_HALF,
    BOX_HALF, /* 6: -++ */
    BOX_HALF,
    BOX_HALF,
    BOX_HALF, /* 7: +++ */
};

/* 12 box edges as pairs of corner indices. */
static const int box_edges[12 * 2] = {
    0,
    1,
    1,
    5,
    5,
    4,
    4,
    0, /* bottom face */
    2,
    3,
    3,
    7,
    7,
    6,
    6,
    2, /* top face    */
    0,
    2,
    1,
    3,
    4,
    6,
    5,
    7, /* verticals   */
};

static ParticleSystem *ps;
static FFTTrack *snare;
static OnsetDetector snare_onset;
static unsigned int rng;

static void set_palette(void)
{
  /* Cool ramp: 0..63 black->deep blue, 64..127 deep blue->cyan,
   * 128..191 cyan->white, 192..255 sustained white. Mirrors the
   * gradient shape used in particles.c so the colour-ramp helper in
   * the particle system walks cleanly across it. */
  int i;
  for (i = 0; i < 256; i++)
  {
    int r, g, b;
    if (i < 64)
    {
      r = 0;
      g = 0;
      b = i;
    }
    else if (i < 128)
    {
      r = 0;
      g = i - 64;
      b = 63;
    }
    else if (i < 192)
    {
      r = i - 128;
      g = 63;
      b = 63;
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
  rng = 0x5EED1234u;
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
  unsigned char ax = (unsigned char)(ctx->frame);
  int corner_view[8 * 3];
  int corner_screen[8 * 2];
  signed char corner_visible[8];
  int e, i;

  /* Continuous trickle: a couple of particles per frame at the centre
   * with random velocity in all directions. */
  particles_emit(ps,
                 0, 0, 0,
                 0, 0, 0,
                 INT_TO_FP(1) / 4, /* spread per axis */
                 80, 200,          /* lifetime range */
                 200,              /* base colour: bright cyan */
                 -200,             /* ramp toward black */
                 1,                /* size: single pixel */
                 EMIT_RATE, &rng);

  /* Snare-driven burst: large injection of particles at the centre. */
  e = (int)fft_at(snare, ctx->timeline_frame);
  if (onset_step(&snare_onset, e))
  {
    particles_emit(ps,
                   0, 0, 0,
                   0, 0, 0,
                   INT_TO_FP(1) / 2, /* wider scatter */
                   60, 120,
                   240, /* hot near-white */
                   -240,
                   2, /* 2x2 blocks */
                   BURST_COUNT, &rng);
  }

  particles_update(ps, &forces);

  /* Six-plane bounding box. All six planes have d = -BOX_HALF because
   * each face is BOX_HALF away from the origin along its inward
   * normal. */
  particles_bounce_plane(ps, 0, FP_ONE, 0, -BOX_HALF, BOX_BOUNCE);  /* floor   */
  particles_bounce_plane(ps, 0, -FP_ONE, 0, -BOX_HALF, BOX_BOUNCE); /* ceiling */
  particles_bounce_plane(ps, FP_ONE, 0, 0, -BOX_HALF, BOX_BOUNCE);  /* left    */
  particles_bounce_plane(ps, -FP_ONE, 0, 0, -BOX_HALF, BOX_BOUNCE); /* right   */
  particles_bounce_plane(ps, 0, 0, FP_ONE, -BOX_HALF, BOX_BOUNCE);  /* back    */
  particles_bounce_plane(ps, 0, 0, -FP_ONE, -BOX_HALF, BOX_BOUNCE); /* front   */

  memset(backbuffer, 0, VGA_SIZE);

  /* Wireframe: project the eight box corners with the same camera
   * transform as the particles, then draw the twelve edges. */
  transform_points(corner_view, box_corners, 8, ay, ax, camera.cam_z);
  project_points(&camera, corner_view, 8, corner_screen, corner_visible);
  for (i = 0; i < 12; i++)
  {
    int a = box_edges[i * 2];
    int b = box_edges[i * 2 + 1];
    if (corner_visible[a] && corner_visible[b])
    {
      draw_line(backbuffer,
                corner_screen[a * 2], corner_screen[a * 2 + 1],
                corner_screen[b * 2], corner_screen[b * 2 + 1],
                WIRE_COLOR);
    }
  }

  particles_draw(ps, &camera, ay, ax, backbuffer);

  font_draw(&font_default, backbuffer, 4, 4, 255, "swarm.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene swarm_scene = {setup, init, shutdown, render};
