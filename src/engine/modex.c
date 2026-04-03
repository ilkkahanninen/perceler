/*
 * Mode-X 320x240x256 VGA routines
 *
 * Mode-X uses planar memory: 4 planes, each pixel's plane is determined
 * by (x % 4). Under DOS/32A flat model, VGA memory at physical 0xA0000
 * is accessed directly as a near pointer.
 */

#include "modex.h"

#include <conio.h>
#include <i86.h>
#include <string.h>

/* VGA ports */
#define VGA_SC_INDEX 0x3C4
#define VGA_SC_DATA 0x3C5
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_INPUT_STATUS 0x3DA
#define VGA_MISC_WRITE 0x3C2
#define VGA_DAC_WRITE 0x3C8
#define VGA_DAC_DATA 0x3C9

#define VGA_MEM MODEX_VGAMEM

void modex_init(void) {
  union REGS regs;

  /* Start with standard mode 13h, then tweak to Mode-X */
  regs.w.ax = 0x0013;
  int386(0x10, &regs, &regs);

  /* Switch to 480-line timing (25MHz dot clock, negative vsync polarity) */
  outp(VGA_MISC_WRITE, 0xE3);

  /* Turn off Chain-4 in Sequencer Memory Mode register */
  outpw(VGA_SC_INDEX, 0x0604);

  /* Enable all 4 planes for writing, then clear all VGA memory */
  outpw(VGA_SC_INDEX, 0x0F02);
  memset((void *)VGA_MEM, 0, 0x10000);

  /* Underline Location (CRTC 0x14) = 0x00: disable doubleword addressing */
  outpw(VGA_CRTC_INDEX, 0x0014);
  /* Mode Control (CRTC 0x17) = 0xE3: byte mode */
  outpw(VGA_CRTC_INDEX, 0xE317);

  /*
   * Reprogram CRTC for 240 visible lines.
   * 480-line timing / 2 scan lines per row (max scan line = 1) = 240 rows.
   */

  /* Unlock CRTC registers 0-7 (clear write protect in reg 0x11) */
  outpw(VGA_CRTC_INDEX, 0x2C11);

  outpw(VGA_CRTC_INDEX, 0x0D06); /* Vertical Total */
  outpw(VGA_CRTC_INDEX, 0x3E07); /* Overflow */
  outpw(VGA_CRTC_INDEX,
        0x4109); /* Max Scan Line: 2 scans/row, no double-scan */
  outpw(VGA_CRTC_INDEX, 0xEA10); /* Vertical Retrace Start */
  outpw(VGA_CRTC_INDEX,
        0xAC11); /* Vertical Retrace End (re-enables write protect) */
  outpw(VGA_CRTC_INDEX, 0xDF12); /* Vertical Display End */
  outpw(VGA_CRTC_INDEX, 0xE715); /* Vertical Blank Start */
  outpw(VGA_CRTC_INDEX, 0x0616); /* Vertical Blank End */
}

void modex_exit(void) {
  union REGS regs;

  /* Restore text mode 3 */
  regs.w.ax = 0x0003;
  int386(0x10, &regs, &regs);
}

void modex_vsync(void) {
  while (inp(VGA_INPUT_STATUS) & 0x08)
    ;
  while (!(inp(VGA_INPUT_STATUS) & 0x08))
    ;
}

void modex_setpage(unsigned int offset) {
  outpw(VGA_CRTC_INDEX, (offset & 0xFF00) | 0x0C);
  outpw(VGA_CRTC_INDEX, ((offset & 0x00FF) << 8) | 0x0D);
}

void modex_setplane(int plane) {
  outpw(VGA_SC_INDEX, (0x100 << (plane & 3)) | 0x02);
}

void modex_putpixel(int x, int y, unsigned char color, unsigned int page) {
  outpw(VGA_SC_INDEX, (0x100 << (x & 3)) | 0x02);

  /* Each row is 80 bytes (320/4) */
  VGA_MEM[page + (unsigned int)y * 80 + (unsigned int)(x >> 2)] = color;
}

void modex_clear(unsigned char color, unsigned int page) {
  /* Enable all 4 planes */
  outpw(VGA_SC_INDEX, 0x0F02);

  memset((void *)(VGA_MEM + page), color, MODEX_PAGE_SIZE);
}

void modex_setpalette(unsigned char index, unsigned char r, unsigned char g,
                      unsigned char b) {
  outp(VGA_DAC_WRITE, index);
  outp(VGA_DAC_DATA, r);
  outp(VGA_DAC_DATA, g);
  outp(VGA_DAC_DATA, b);
}
