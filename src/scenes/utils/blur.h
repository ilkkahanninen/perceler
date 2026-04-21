#ifndef BLUR_H
#define BLUR_H

/*
 * Separable 3-tap box blur on an 8-bit indexed VGA backbuffer.
 *
 * Blurs palette indices directly using a (1, 2, 1) / 4 kernel, horizontal
 * then vertical. Looks correct with gradient palettes (e.g. grayscale
 * ramps); for arbitrary palettes, blur the RGB values instead.
 *
 * Uses an internal 3-row rolling buffer, no heap allocation. The entire
 * working set fits in L1 cache.
 */
void blur(unsigned char *buf);

#endif
