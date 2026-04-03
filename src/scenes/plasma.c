/*
 * Plasma effect - classic sine-based plasma with color cycling
 *
 * Renders plane-by-plane (4 passes) to minimize VGA port I/O.
 */

#include "plasma.h"

#include "../assets.h"
#include "utils/bitmap.h"
#include "utils/math.h"

#include <modex.h>
#include <stdlib.h>

static Bitmap *hello;

static void plasma_setup(void) {
  hello = bitmap_load(ASSET_HELLO_BMP);
}

static void plasma_init(void) {
  palette_apply(&hello->palette);
}

static void plasma_shutdown(void) {
  bitmap_free(hello);
  hello = NULL;
}

static void plasma_render(unsigned int draw_page, unsigned char frame) {
  int plane, x, y;
  unsigned char frame2 = frame * 2;
  unsigned char frame3 = frame * 3;

  for (plane = 0; plane < 4; plane++) {
    volatile unsigned char *row;

    modex_setplane(plane);
    row = MODEX_VGAMEM + draw_page + (plane >> 2);

    for (y = 0; y < MODEX_HEIGHT; y++) {
      volatile unsigned char *dst = row;
      unsigned char sin_y = sintab[(y + frame) & 0xFF];
      unsigned char idx_add = (unsigned char)((plane + y) / 2 + frame2);
      unsigned char idx_sub = (unsigned char)((plane - y + 256) / 2 + frame3);

      for (x = plane; x < MODEX_WIDTH; x += 4) {
        unsigned char col1 = (sintab[(x + frame) & 0xFF] + sin_y) >> 4;
        unsigned char col2 = (sintab[idx_add] + sintab[idx_sub]) & 0xF0;
        *dst++ = col1 | col2;
        idx_add += 2;
        idx_sub += 2;
      }

      row += 80;
    }
  }

  if (hello)
    bitmap_blit(hello, (MODEX_WIDTH - hello->width) / 2,
                (MODEX_HEIGHT - hello->height) / 2, draw_page);
}

const Scene plasma_scene = {plasma_setup, plasma_init, plasma_shutdown,
                            plasma_render};
