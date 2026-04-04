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

#define TEX_SIZE 256
#define CX (VGA_WIDTH / 2)
#define CY (VGA_HEIGHT / 2)

#define PI 3.14159265

/* Lookup tables: angle and distance for each screen pixel */
static unsigned char *angle_tab;
static unsigned char *dist_tab;

/* Procedural texture */
static unsigned char texture[TEX_SIZE * TEX_SIZE];

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

static void set_tunnel_palette(void)
{
  int i;
  for (i = 0; i < 256; i++)
  {
    unsigned char r, g, b;
    r = (unsigned char)(32.0 * (1.0 + sin(i * PI / 128.0)));
    g = (unsigned char)(20.0 * (1.0 + sin(i * PI / 64.0 + 2.0)));
    b = (unsigned char)(32.0 * (1.0 + sin(i * PI / 96.0 + 4.0)));
    vga_setpalette((unsigned char)i, r, g, b);
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
  set_tunnel_palette();
}

static void tunnel_shutdown(void)
{
  free(angle_tab);
  free(dist_tab);
  angle_tab = 0;
  dist_tab = 0;
}

static void tunnel_render(unsigned char *backbuffer, unsigned int frame)
{
  int x, y;
  unsigned char shift_u = frame * 2;
  unsigned char shift_v = frame;
  unsigned char *dst = backbuffer;

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
  vga_vsync();
  vga_blit(backbuffer);
}

const Scene tunnel_scene = {tunnel_setup, tunnel_init, tunnel_shutdown,
                            tunnel_render};
