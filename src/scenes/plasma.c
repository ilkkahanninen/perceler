/*
 * Plasma effect - classic sine-based plasma with color cycling
 */

#include "plasma.h"

#include "../assets.h"
#include "utils/bitmap.h"
#include "utils/blur.h"
#include "utils/dither.h"
#include "utils/math.h"
#include "utils/mem.h"

#include <stdlib.h>
#include <string.h>
#include <vga.h>
#include "../utils/tween.h"
#include "utils/font.h"

static Bitmap *image;
static unsigned char *radial_tab;
static unsigned char *overlay; /* full-screen bitmap layer; 0 = transparent */

/* Tween the title's X position on a ~6-second timeline:
 *   slide in from off-screen-left (ease-out so it decelerates on arrival),
 *   hold near the left margin (TWEEN_STEP holds the value until the next key),
 *   slide out to the right with an ease-in accelerating exit. */
static const Keyframe title_x_keys[] = {
    {0, INT_TO_FP(-80), TWEEN_EASE_OUT},  /* off-screen left */
    {1200, INT_TO_FP(6), TWEEN_STEP},     /* slid in, pinned */
    {5000, INT_TO_FP(6), TWEEN_EASE_IN},  /* still pinned; start accel */
    {6200, INT_TO_FP(330), TWEEN_LINEAR}, /* off-screen right */
};
static const Tween title_x = TWEEN(title_x_keys);

static void plasma_setup(void)
{
  int x, y;
  unsigned int image_x, image_y;

  image = bitmap_load(ASSET_JML_BMP);
  image_x = (VGA_WIDTH - image->width) / 2;
  image_y = (VGA_HEIGHT - image->height) / 2;

  /* Stagger overlay and radial_tab into different cache sets from each
   * other and from the backbuffer, so the lockstep reads in the render
   * loop don't conflict-miss in a 2-way L1. */
  overlay = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_SCENE_0);
  memset(overlay, 0, VGA_SIZE);
  bitmap_blit_to_buffer(image, overlay, VGA_WIDTH, VGA_HEIGHT, image_x,
                        image_y);

  radial_tab = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_SCENE_1);
  for (y = 0; y < VGA_HEIGHT; y++)
  {
    int cy = y - 100;
    for (x = 0; x < VGA_WIDTH; x++)
    {
      int cx = x - 160;
      radial_tab[y * VGA_WIDTH + x] = sintab[((cx * cy * 64) >> 8) & 0xFF] >> 2;
    }
  }
}

static void plasma_set_palette(void)
{
  int i;
  for (i = 0; i < 128; i++)
  {
    unsigned char r = 0, g = 0, b = 0;
    if (i < 32)
    {
      /* Black to blue (0-31) */
      b = (unsigned char)(i * 2);
    }
    else if (i < 64)
    {
      /* Blue to red (32-63) */
      b = (unsigned char)((63 - i) * 2);
      r = (unsigned char)((i - 32) * 2);
    }
    else if (i < 96)
    {
      /* Red to yellow (64-95) */
      r = 63;
      g = (unsigned char)((i - 64) * 2) >> 1;
    }
    else
    {
      /* Yellow to black (96-127) */
      r = g = (unsigned char)((127 - i) * 2);
      g >>= 1;
    }
    vga_setpalette((unsigned char)i, r, g, b);
  }

  /* Greyscale*/
  for (i = 0; i < 64; i++)
  {
    unsigned char c = i;
    vga_setpalette((unsigned char)(128 + i), c, c, c);
  }
}

static void plasma_init(const RenderContext *ctx)
{
  (void)ctx;
  palette_apply(&image->palette);
  plasma_set_palette();
}

static void plasma_shutdown(void)
{
  bitmap_free(image);
  image = NULL;
  mem_free_aligned(radial_tab);
  radial_tab = NULL;
  mem_free_aligned(overlay);
  overlay = NULL;
}

static void plasma_render(const RenderContext *ctx)
{
  unsigned char *backbuffer = ctx->backbuffer;
  unsigned int frame = ctx->frame;
  int x, y;
  unsigned char *dst = backbuffer;
  unsigned int f2 = frame * 3;
  const unsigned char *rad = radial_tab;
  const unsigned char *ovl = overlay;

  for (y = 0; y < VGA_HEIGHT; y++)
  {
    unsigned char sy1 = sintab[((y << 1) + frame) & 0xFF];
    unsigned char sy2 = sintab[((y * 5 + f2) >> 1) & 0xFF];
    unsigned char t = (y + frame) & 0xFF;
    unsigned char threshold = (t < 128) ? (t << 1) : ((255 - t) << 1);

    for (x = 0; x < VGA_WIDTH; x++)
    {
      unsigned char bmp_pixel = *ovl++;
      if (bmp_pixel != 0 && dither_threshold(dither_cluster8x8, x, y, threshold))
      {
        *dst++ = bmp_pixel;
        rad++;
      }
      else
      {
        unsigned char v;
        v = sintab[((x << 1) + frame) & 0xFF];
        v += sy1;
        v += sintab[((x + y + f2) >> 1) & 0xFF];
        v += sy2;
        v += *rad++;
        *dst++ = v >> 1;
      }
    }
  }

  blur(backbuffer);

  /* Slide the title via a keyframed tween. */
  font_draw(&font_default, backbuffer,
            tween_at_int(&title_x, ctx->ms), 4, 255, "plasma.c");

  vga_vsync();
  vga_blit(backbuffer);
}

const Scene plasma_scene = {plasma_setup, plasma_init, plasma_shutdown,
                            plasma_render};
