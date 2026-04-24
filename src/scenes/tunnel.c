/*
 * Tunnel effect - textured tunnel flythrough
 *
 * Uses precomputed angle and distance lookup tables to map each
 * screen pixel into a 256x256 texture space. Animating the texture
 * offset each frame creates the illusion of flying through a tunnel.
 */

#include "tunnel.h"

#include <math.h>
#include <vga.h>
#include <stdlib.h>
#include "utils/font.h"
#include "utils/palette.h"

#define TEX_SIZE 256
#define CX (VGA_WIDTH / 2)
#define CY (VGA_HEIGHT / 2)

#define PI 3.14159265

/* Lookup tables: angle and distance for each screen pixel */
static unsigned char *angle_tab;
static unsigned char *dist_tab;

/* Procedural texture */
static unsigned char texture[TEX_SIZE * TEX_SIZE];

/* Two endpoint palettes that palette_lerp() interpolates between each
 * frame. pal_current is the blended DAC state written to the VGA. */
static Palette pal_warm;
static Palette pal_cool;
static Palette pal_current;

static void generate_texture(void)
{
  int u, v;
  for (v = 0; v < TEX_SIZE; v++)
  {
    for (u = 0; u < TEX_SIZE; u++)
    {
      /* XOR pattern with some variation */
      texture[v * TEX_SIZE + u] = (unsigned char)((u ^ v) + (u * v >> 6));
    }
  }
}

static void generate_tables(void)
{
  int x, y;
  angle_tab = (unsigned char *)malloc(VGA_WIDTH * VGA_HEIGHT);
  dist_tab = (unsigned char *)malloc(VGA_WIDTH * VGA_HEIGHT);

  for (y = 0; y < VGA_HEIGHT; y++)
  {
    for (x = 0; x < VGA_WIDTH; x++)
    {
      double dx = (double)(x - CX);
      double dy = (double)(y - CY);
      double dist = sqrt(dx * dx + dy * dy);
      double ang = atan2(dy, dx);
      int idx = y * VGA_WIDTH + x;

      /* Map angle to 0-255 */
      angle_tab[idx] = (unsigned char)(ang * 128.0 / PI + 128.0);

      /* Map distance: inversely proportional for tunnel depth */
      if (dist < 1.0)
        dist = 1.0;
      dist_tab[idx] = (unsigned char)(2048.0 / dist);
    }
  }
}

static void build_palettes(void)
{
  int i;
  for (i = 0; i < 256; i++)
  {
    /* Warm: red/orange with a weak yellow undertone, no blue. */
    pal_warm.entries[i][0] =
        (unsigned char)(32.0 * (1.0 + sin(i * PI / 128.0)));
    pal_warm.entries[i][1] =
        (unsigned char)(12.0 * (1.0 + sin(i * PI / 64.0)));
    pal_warm.entries[i][2] = 0;

    /* Cool: blue/cyan, no red. */
    pal_cool.entries[i][0] = 0;
    pal_cool.entries[i][1] =
        (unsigned char)(12.0 * (1.0 + sin(i * PI / 64.0 + 1.5)));
    pal_cool.entries[i][2] =
        (unsigned char)(32.0 * (1.0 + sin(i * PI / 96.0 + 3.0)));
  }
}

static void tunnel_setup(void)
{
  generate_texture();
  generate_tables();
}

static void tunnel_init(unsigned char *backbuffer)
{
  (void)backbuffer;
  build_palettes();
  /* Seed pal_current so the very first frame isn't drawn against the
   * zero-initialised (all-black) palette. */
  pal_current = pal_warm;
  palette_apply(&pal_current);
}

static void tunnel_shutdown(void)
{
  free(angle_tab);
  free(dist_tab);
  angle_tab = 0;
  dist_tab = 0;
}

static void tunnel_render(unsigned char *backbuffer, unsigned int frame,
                          unsigned int timeline_frame)
{
  int x, y;
  unsigned char shift_u = frame * 2;
  unsigned char shift_v = frame;
  unsigned char *dst = backbuffer;
  (void)timeline_frame;

  for (y = 0; y < VGA_HEIGHT; y++)
  {
    for (x = 0; x < VGA_WIDTH; x++)
    {
      int idx = y * VGA_WIDTH + x;
      unsigned char u = angle_tab[idx] + shift_u;
      unsigned char v = dist_tab[idx] + shift_v;
      *dst++ = texture[v * TEX_SIZE + u];
    }
  }

  font_draw(&font_default, backbuffer, 4, 4, 255, "tunnel.c");

  /* Crossfade warm ↔ cool on a ~2-second cycle (120 frames @ 60 fps).
   * t oscillates 0..256: 0 = full warm, 256 = full cool. */
  {
    int t = 128 + (int)(128.0 * sin(frame * (2.0 * PI / 120.0)));
    palette_lerp(&pal_current, &pal_warm, &pal_cool, t);
  }

  vga_vsync();
  palette_apply(&pal_current);
  vga_blit(backbuffer);
}

const Scene tunnel_scene = {tunnel_setup, tunnel_init, tunnel_shutdown,
                            tunnel_render};
