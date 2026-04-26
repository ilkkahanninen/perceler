#ifndef BLUR_H
#define BLUR_H

/*
 * Separable 3-tap box blur on an 8-bit indexed VGA backbuffer.
 *
 * Blurs palette indices directly using a (1, 2, 1) / 4 kernel,
 * horizontal then vertical. Produces visually correct results with
 * gradient palettes (greyscale ramps, etc.); for arbitrary palettes
 * the result is the index-space average, not the RGB average.
 */
void blur(unsigned char *buf);

#endif
