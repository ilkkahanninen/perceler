/*
 * Plasma effect - classic sine-based plasma with color cycling
 */

#include "plasma.h"

#include "../assets.h"
#include "utils/bitmap.h"
#include "utils/math.h"

#include <stdlib.h>
#include <vga.h>

static Bitmap *hello;
static unsigned char *backbuffer;
static unsigned int image_x, image_y;

static void plasma_setup(void) {
  hello = bitmap_load(ASSET_HELLO_BMP);
  image_x = (VGA_WIDTH - hello->width) / 2;
  image_y = (VGA_HEIGHT - hello->height) / 2;
  backbuffer = malloc(VGA_SIZE);
}

static void plasma_init(void) {
  palette_apply(&hello->palette);
}

static void plasma_shutdown(void) {
  bitmap_free(hello);
  hello = NULL;
  free(backbuffer);
  backbuffer = NULL;
}

static void plasma_render(unsigned char frame) {
  int x, y;
  unsigned char *dst = backbuffer;
  const unsigned char *st = sintab;

  for (y = 0; y < VGA_HEIGHT; y++) {
    for (x = 0; x < VGA_WIDTH; x++) {
      *dst++ = (x + y + frame) & 0xFF;
    }
  }

  bitmap_blit_to_buffer(hello, backbuffer, VGA_WIDTH, VGA_HEIGHT, image_x,
                        image_y);

  vga_blit(backbuffer);
}

const Scene plasma_scene = {plasma_setup, plasma_init, plasma_shutdown,
                            plasma_render};
