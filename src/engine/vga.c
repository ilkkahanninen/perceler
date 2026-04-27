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

void vga_init(void)
{
  union REGS regs;
  regs.w.ax = 0x0013;
  int386(0x10, &regs, &regs);
}

void vga_exit(void)
{
  union REGS regs;
  regs.w.ax = 0x0003;
  int386(0x10, &regs, &regs);
}

/* Render-target stack — see vga.h. Capacity is fixed at 4. */
#define VGA_TARGET_STACK_DEPTH 4
static unsigned char *target_stack[VGA_TARGET_STACK_DEPTH];
static int target_stack_top;

void vga_push_render_target(unsigned char *target)
{
  if (target_stack_top < VGA_TARGET_STACK_DEPTH)
    target_stack[target_stack_top++] = target;
}

void vga_pop_render_target(void)
{
  if (target_stack_top > 0)
    target_stack_top--;
}

void vga_blit(const unsigned char *buf)
{
  void *dst = (target_stack_top > 0)
                  ? (void *)target_stack[target_stack_top - 1]
                  : (void *)VGA_MEM;
  memcpy(dst, buf, VGA_SIZE);
}

void vga_vsync(void)
{
  /* Skip the wait while a custom render target is active — there's no
   * beam to race when blitting offscreen. */
  if (target_stack_top > 0)
    return;
  while (inp(VGA_INPUT_STATUS) & 0x08)
    ;
  while (!(inp(VGA_INPUT_STATUS) & 0x08))
    ;
}

void vga_putpixel(int x, int y, unsigned char color)
{
  VGA_MEM[(unsigned int)y * VGA_WIDTH + (unsigned int)x] = color;
}

void vga_setpalette(unsigned char index, unsigned char r, unsigned char g,
                    unsigned char b)
{
  outp(VGA_DAC_WRITE, index);
  outp(VGA_DAC_DATA, r);
  outp(VGA_DAC_DATA, g);
  outp(VGA_DAC_DATA, b);
}

void vga_blit_2x_strided(const unsigned char *src, int src_stride,
                         unsigned char *dst)
{
  int y;
  /* SWAR: read 4 source bytes per iteration, expand each byte to two
   * adjacent bytes, and write the resulting 8 bytes to two scanlines.
   * VGA_HALF_WIDTH (160) is a multiple of 4; src_stride must be too. */
  for (y = 0; y < VGA_HALF_HEIGHT; y++)
  {
    const unsigned int *s = (const unsigned int *)(src + y * src_stride);
    unsigned int *d0 = (unsigned int *)(dst + (y * 2) * VGA_WIDTH);
    unsigned int *d1 = (unsigned int *)(dst + (y * 2 + 1) * VGA_WIDTH);
    int n = VGA_HALF_WIDTH >> 2; /* 40 dwords per source row */
    while (n--)
    {
      unsigned int p = *s++;
      /* p packs source bytes 0xDDCCBBAA in little-endian order.
       * `low` doubles bytes A,B  -> 0xBBBBAAAA.
       * `high` doubles bytes C,D -> 0xDDDDCCCC. */
      unsigned int low =
          (p & 0x000000FFu) | ((p & 0x000000FFu) << 8) |
          ((p & 0x0000FF00u) << 8) | ((p & 0x0000FF00u) << 16);
      unsigned int high =
          ((p & 0x00FF0000u) >> 16) | ((p & 0x00FF0000u) >> 8) |
          ((p & 0xFF000000u) >> 8) | (p & 0xFF000000u);
      d0[0] = low;
      d0[1] = high;
      d1[0] = low;
      d1[1] = high;
      d0 += 2;
      d1 += 2;
    }
  }
}

void vga_blit_2x_to_buffer(const unsigned char *src, unsigned char *dst)
{
  vga_blit_2x_strided(src, VGA_HALF_WIDTH, dst);
}

void vga_blit_rows(const unsigned char *buf, int y_start, int y_count)
{
  const unsigned int *src;
  unsigned int *dst;
  int dwords;

  /* Clamp to the visible window. */
  if (y_start < 0)
  {
    y_count += y_start;
    y_start = 0;
  }
  if (y_count <= 0 || y_start >= VGA_HEIGHT)
    return;
  if (y_start + y_count > VGA_HEIGHT)
    y_count = VGA_HEIGHT - y_start;

  /* SWAR: copy 4 bytes per iteration. y_start * VGA_WIDTH is always a
   * multiple of 4 because VGA_WIDTH=320 is, so both pointers are
   * 4-byte aligned given an aligned `buf`. */
  src = (const unsigned int *)(buf + y_start * VGA_WIDTH);
  dst = (unsigned int *)((unsigned char *)VGA_MEM + y_start * VGA_WIDTH);
  dwords = (y_count * VGA_WIDTH) >> 2;
  while (dwords--)
    *dst++ = *src++;
}
