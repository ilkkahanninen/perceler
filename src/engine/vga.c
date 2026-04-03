/*
 * VGA Mode 13h - 320x200x256 linear framebuffer
 *
 * Under DOS/32A flat model, VGA memory at physical 0xA0000
 * is accessed directly as a near pointer.
 */

#include "vga.h"

#include <conio.h>
#include <i86.h>

/* VGA ports */
#define VGA_INPUT_STATUS 0x3DA
#define VGA_DAC_WRITE 0x3C8
#define VGA_DAC_DATA 0x3C9

void vga_init(void) {
  union REGS regs;
  regs.w.ax = 0x0013;
  int386(0x10, &regs, &regs);
}

void vga_exit(void) {
  union REGS regs;
  regs.w.ax = 0x0003;
  int386(0x10, &regs, &regs);
}

void vga_vsync(void) {
  while (inp(VGA_INPUT_STATUS) & 0x08)
    ;
  while (!(inp(VGA_INPUT_STATUS) & 0x08))
    ;
}

void vga_putpixel(int x, int y, unsigned char color) {
  VGA_MEM[(unsigned int)y * VGA_WIDTH + (unsigned int)x] = color;
}

void vga_setpalette(unsigned char index, unsigned char r, unsigned char g,
                    unsigned char b) {
  outp(VGA_DAC_WRITE, index);
  outp(VGA_DAC_DATA, r);
  outp(VGA_DAC_DATA, g);
  outp(VGA_DAC_DATA, b);
}
