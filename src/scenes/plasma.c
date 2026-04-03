/*
 * Plasma effect - classic sine-based plasma with color cycling
 */

#include "plasma.h"

#include "../assets.h"
#include "utils/bitmap.h"
#include "utils/math.h"

#include <vga.h>
#include <stdlib.h>

static Bitmap *hello;
static unsigned char *backbuffer;

static void plasma_setup(void) {
  hello = bitmap_load(ASSET_HELLO_BMP);
  backbuffer = malloc(VGA_SIZE);
}

static void plasma_init(void) { palette_apply(&hello->palette); }

static void plasma_shutdown(void) {
  bitmap_free(hello);
  hello = NULL;
  free(backbuffer);
  backbuffer = NULL;
}

static void plasma_render(unsigned char frame) {
  int x, y;
  unsigned char frame2 = frame * 2;
  unsigned char frame3 = frame * 3;
  unsigned char *dst = backbuffer;

  for (y = 0; y < VGA_HEIGHT; y++) {
    unsigned char sin_y = sintab[(y + frame) & 0xFF];
    unsigned char idx_add = (unsigned char)(y / 2 + frame2);
    unsigned char idx_sub = (unsigned char)((-y + 256) / 2 + frame3);

    for (x = 0; x < VGA_WIDTH; x++) {
      unsigned char col1 = (sintab[(x + frame) & 0xFF] + sin_y) >> 4;
      unsigned char col2 = (sintab[idx_add] + sintab[idx_sub]) & 0xF0;
      *dst++ = col1 | col2;
      idx_add += (x & 1);
      idx_sub += (x & 1);
    }
  }

  if (hello)
    bitmap_blit_to_buffer(hello, backbuffer, VGA_WIDTH, VGA_HEIGHT,
                          (VGA_WIDTH - hello->width) / 2,
                          (VGA_HEIGHT - hello->height) / 2);

  vga_blit(backbuffer);
}

const Scene plasma_scene = {plasma_setup, plasma_init, plasma_shutdown,
                            plasma_render};
