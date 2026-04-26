#ifndef FONT_H
#define FONT_H

#include "../../assets.h"

/*
 * Monospace bitmap-font text renderer.
 *
 * A Font stores glyph bitmaps packed LSB-first (bit 0 = leftmost
 * pixel), with `glyph_w / 8` bytes per row and `glyph_h` rows per
 * glyph. Glyphs cover a contiguous ASCII range starting at
 * `first_char`. `glyph_w` must be a multiple of 8.
 *
 * Layout sizes:
 *   glyph_w=8,  glyph_h=8  -> 1 byte/row  ×  8 rows =  8 bytes/glyph
 *   glyph_w=16, glyph_h=16 -> 2 bytes/row × 16 rows = 32 bytes/glyph
 *   glyph_w=24, glyph_h=32 -> 3 bytes/row × 32 rows = 96 bytes/glyph
 *
 * `font_default` is a built-in public-domain 8×8 IBM-style font
 * covering ASCII 32 ('space') through 127.
 *
 * Rendering is a transparent blit: zero bits are skipped, set bits are
 * written as the caller-supplied palette index. Bounds-clipped to
 * 320×200.
 */

typedef struct
{
  const unsigned char *glyphs; /* num_chars * (glyph_w/8) * glyph_h bytes */
  int glyph_w;                 /* must be a multiple of 8 */
  int glyph_h;
  int first_char; /* ASCII code of glyphs[0] */
  int num_chars;
} Font;

extern const Font font_default;

/* Render a null-terminated ASCII string at (x, y) on `buf` using `color`
 * as the foreground palette index. Characters outside the font's
 * `first_char..first_char+num_chars-1` range advance the cursor but
 * emit no pixels. */
void font_draw(const Font *font, unsigned char *buf, int x, int y,
               unsigned char color, const char *s);

/* Width of `s` in pixels. Monospace: strlen(s) * font->glyph_w. */
int font_width(const Font *font, const char *s);

/* Load a custom font from an .fnt asset produced by tools/font.py. The
 * returned Font and its glyph data are heap-allocated; pair with
 * font_free(). Returns NULL on error. */
Font *font_load(Asset asset);

/* Free a Font returned by font_load(). Does nothing on NULL. */
void font_free(Font *font);

#endif
