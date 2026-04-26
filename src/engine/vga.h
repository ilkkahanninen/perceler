#ifndef VGA_H
#define VGA_H

#include <string.h>

#define VGA_WIDTH  320
#define VGA_HEIGHT 200
#define VGA_SIZE   (VGA_WIDTH * VGA_HEIGHT)

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

#endif
