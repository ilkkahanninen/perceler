#ifndef VGA_H
#define VGA_H

#include <string.h>

#define VGA_WIDTH  320
#define VGA_HEIGHT 200
#define VGA_SIZE   (VGA_WIDTH * VGA_HEIGHT)

/* Half-resolution dimensions for scenes that render at 160x100 and
 * scale up via vga_blit_2x_to_buffer(). */
#define VGA_HALF_WIDTH  (VGA_WIDTH  / 2)   /* 160 */
#define VGA_HALF_HEIGHT (VGA_HEIGHT / 2)   /* 100 */
#define VGA_HALF_SIZE   (VGA_HALF_WIDTH * VGA_HALF_HEIGHT)

/* VGA frame buffer as a flat near pointer (DOS/32A maps the low 1 MB
 * linearly). */
#define VGA_MEM ((volatile unsigned char *)0xA0000)

/* Switch to mode 13h (320×200×256). */
void vga_init(void);

/* Restore the previous video mode. */
void vga_exit(void);

/* Block until the next vertical retrace begins. */
void vga_vsync(void);

/* Write `color` at (x, y) directly to VGA memory. No clipping. */
void vga_putpixel(int x, int y, unsigned char color);

/* Set a single DAC entry (channels in 0-63 range). */
void vga_setpalette(unsigned char index, unsigned char r, unsigned char g,
                    unsigned char b);

/* Fill VGA memory with `color`. */
#define vga_clear(color) memset((void *)VGA_MEM, (color), VGA_SIZE)

/* Copy a 320×200 backbuffer to VGA memory. */
#define vga_blit(buf) memcpy((void *)VGA_MEM, (buf), VGA_SIZE)

/* Pixel-doubled blit: copy a VGA_HALF_WIDTH × VGA_HALF_HEIGHT source
 * buffer into a 320×200 destination, expanding each source pixel into
 * a 2×2 block. Useful for scenes that render an expensive effect
 * (raytracing, software shading, etc.) at half resolution and scale
 * up to fill the screen. The destination is typically the scene
 * backbuffer — pass (unsigned char *)VGA_MEM to write directly to the
 * screen instead. Both `src` and `dst` must be 4-byte aligned. */
void vga_blit_2x_to_buffer(const unsigned char *src, unsigned char *dst);

/* Copy a row range from a backbuffer to VGA memory. Pushes only the
 * lines a scene actually rewrote — useful for interleaved rendering
 * where each frame touches every other scanline.
 *
 * `y_start` and `y_count` are clamped to the screen, so out-of-range
 * arguments are harmless no-ops. `buf` must be 4-byte aligned. */
void vga_blit_rows(const unsigned char *buf, int y_start, int y_count);

#endif
