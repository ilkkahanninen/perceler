#ifndef VGA_H
#define VGA_H

#include <string.h>

#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_SIZE (VGA_WIDTH * VGA_HEIGHT)

/* VGA memory as a near pointer (flat model, DOS/32A maps low 1MB linearly) */
#define VGA_MEM ((volatile unsigned char *)0xA0000)

void vga_init(void);
void vga_exit(void);
void vga_vsync(void);
void vga_putpixel(int x, int y, unsigned char color);
#define vga_clear(color) memset((void *)VGA_MEM, (color), VGA_SIZE)
#define vga_blit(buf)    memcpy((void *)VGA_MEM, (buf), VGA_SIZE)
void vga_setpalette(unsigned char index, unsigned char r, unsigned char g,
                    unsigned char b);

#endif
