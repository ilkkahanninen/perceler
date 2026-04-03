/*
 * sb16.c - Sound Blaster 16 driver for DOS/32A (Open Watcom)
 *
 * Uses DMA channel 5 (16-bit, auto-initialize) with double buffering.
 * The IRQ handler sets a flag; the actual buffer fill happens in
 * sb16_update() which must be called from the main loop.
 *
 * Configuration is read from the BLASTER environment variable:
 *   BLASTER=A220 I7 D1 H5 T6
 * Defaults: base=0x220, IRQ=7, DMA16=5
 */

#include "sb16.h"

#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Configuration (populated from BLASTER env or defaults)              */
/* ------------------------------------------------------------------ */
static unsigned short sb_base = 0x220;
static unsigned char sb_irq = 7;
static unsigned char sb_hdma = 5;

/* DSP port shortcuts (macros so they follow sb_base at runtime) */
#define SB_RESET ((unsigned short)(sb_base + 0x06))
#define SB_READ ((unsigned short)(sb_base + 0x0A))
#define SB_WRITE ((unsigned short)(sb_base + 0x0C))
#define SB_RDAVAIL ((unsigned short)(sb_base + 0x0E))
#define SB_ACK16 ((unsigned short)(sb_base + 0x0F))

/* DSP commands */
#define DSP_SET_OUT_RATE 0x41
#define DSP_SPEAKER_ON 0xD1
#define DSP_STOP_16 0xD5
#define DSP_PLAY_16_AI 0xB6 /* 16-bit auto-init output, FIFO */
#define DSP_MODE_S16 0x30   /* signed 16-bit stereo */

/* ------------------------------------------------------------------ */
/* DMA channel 5 ports (second 8237A controller, base 0xC0)            */
/* ------------------------------------------------------------------ */
#define DMA5_MASK 0xD4 /* write (ch & 3) | 4 to mask, ch & 3 to unmask */
#define DMA5_FLIP 0xD8 /* any write resets byte-pointer flip-flop       */
#define DMA5_MODE 0xD6
#define DMA5_ADDR 0xC4 /* word-address: write low then high to SAME port */
#define DMA5_CNT 0xC6  /* word-count-1: write low then high to SAME port */
#define DMA5_PAGE 0x8B /* page register for channel 5                   */

/*
 * Mode byte for channel 5:
 *   bits [7:6] = 01  single mode
 *   bit  5     =  0  address increment
 *   bit  4     =  1  auto-initialize
 *   bits [3:2] = 10  read (memory → device = playback)
 *   bits [1:0] = 01  channel 1 of second ctrl (= channel 5)
 *   = 0101 1001 = 0x59
 */
#define DMA5_MODE_PLAY_AI 0x59

/* ------------------------------------------------------------------ */
/* Double-buffer state                                                  */
/* ------------------------------------------------------------------ */
#define HALF_BYTES                                                             \
  ((unsigned long)(SB16_HALF_SAMPLES) * 2 * 2) /* stereo*16bit */
#define FULL_BYTES (HALF_BYTES * 2)

static sb16_fill_fn g_fill = NULL;
static short *g_buf = NULL;       /* flat protected-mode pointer  */
static unsigned long g_phys = 0;  /* physical address of DMA buf  */
static unsigned short g_dsel = 0; /* DPMI selector (for free)     */

static volatile int g_need = 0; /* set by ISR, cleared by update*/
static volatile int g_half = 0; /* half to fill next (0 or 1)   */

/* ------------------------------------------------------------------ */
/* Saved state for shutdown                                             */
/* ------------------------------------------------------------------ */
static unsigned short g_old_sel = 0;
static unsigned long g_old_off = 0;
static unsigned char g_old_irq_mask = 0;
static unsigned char g_irq_vec = 0;

/* PIC ports */
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

/* ------------------------------------------------------------------ */
/* DSP helpers                                                          */
/* ------------------------------------------------------------------ */
static void dsp_write(unsigned char cmd) {
  while (inp(SB_WRITE) & 0x80)
    ;
  outp(SB_WRITE, cmd);
}

static int dsp_reset(void) {
  int i;
  outp(SB_RESET, 1);
  for (i = 0; i < 100; i++)
    inp(0x80); /* ~3 µs via port-80 delay */
  outp(SB_RESET, 0);
  for (i = 0; i < 65536; i++)
    if ((inp(SB_RDAVAIL) & 0x80) && inp(SB_READ) == 0xAA)
      return 0;
  return -1;
}

/* ------------------------------------------------------------------ */
/* DMA channel 5 programmer                                             */
/* ------------------------------------------------------------------ */
static void dma5_program(unsigned long phys, unsigned long bytes) {
  /*
   * For 16-bit DMA (ch 5):
   *   physical byte address = page<<17 | addr_reg<<1
   *   page     = phys >> 17
   *   addr_reg = (phys >> 1) & 0xFFFF
   *   count    = (bytes / 2) - 1  (word count minus 1)
   */
  unsigned char page = (unsigned char)((phys >> 17) & 0xFF);
  unsigned short waddr = (unsigned short)((phys >> 1) & 0xFFFF);
  unsigned short wcnt = (unsigned short)(bytes / 2 - 1);

  outp(DMA5_MASK, 0x05); /* mask channel 5        */
  outp(DMA5_FLIP, 0x00); /* reset flip-flop       */
  outp(DMA5_MODE, DMA5_MODE_PLAY_AI);
  outp(DMA5_ADDR, (unsigned char)(waddr & 0xFF)); /* low byte  }           */
  outp(DMA5_ADDR, (unsigned char)(waddr >> 8));   /* high byte } same port */
  outp(DMA5_PAGE, page);
  outp(DMA5_CNT, (unsigned char)(wcnt & 0xFF)); /* low byte  }           */
  outp(DMA5_CNT, (unsigned char)(wcnt >> 8));   /* high byte } same port */
  outp(DMA5_MASK, 0x01);                        /* unmask channel 5      */
}

/* ------------------------------------------------------------------ */
/* IRQ handler — minimal: ACK, set flag, EOI                            */
/* ------------------------------------------------------------------ */
static void __interrupt irq_handler(void) {
  inp(SB_ACK16); /* acknowledge SB16 16-bit IRQ */

  g_half ^= 1;
  g_need = 1;

  if (g_irq_vec >= 0x70) { /* slave PIC (IRQ 8-15) */
    outp(PIC2_CMD, PIC_EOI);
  }
  outp(PIC1_CMD, PIC_EOI);
}

/* ------------------------------------------------------------------ */
/* BLASTER env parser                                                   */
/* ------------------------------------------------------------------ */
static void parse_blaster(void) {
  char *p = getenv("BLASTER");
  if (!p)
    return;
  while (*p) {
    char c = *p++;
    if (c == 'A' || c == 'a')
      sb_base = (unsigned short)strtol(p, &p, 16);
    else if (c == 'I' || c == 'i')
      sb_irq = (unsigned char)strtol(p, &p, 10);
    else if (c == 'H' || c == 'h')
      sb_hdma = (unsigned char)strtol(p, &p, 10);
    else
      while (*p && *p != ' ')
        p++;
    while (*p == ' ')
      p++;
  }
  (void)sb_hdma; /* channel fixed to 5 for now; could be generalized */
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
int sb16_init(sb16_fill_fn fill) {
  union REGS r;
  struct SREGS sr;
  unsigned short seg;
  unsigned long phys, alloc_bytes;

  parse_blaster();

  g_fill = fill;
  g_half = 0;
  g_need = 0;

  /* --- Allocate DMA-safe conventional memory via DPMI 0x0100 --- */
  /*
   * Allocate extra to guarantee alignment to 128 KB boundary.
   * (16-bit DMA addr wraps every 128 KB; buffer must not straddle.)
   */
  alloc_bytes = FULL_BYTES + 131072UL;
  r.w.ax = 0x0100;
  r.w.bx = (unsigned short)((alloc_bytes + 15) / 16); /* paragraphs */
  int386(0x31, &r, &r);
  if (r.x.cflag)
    return SB16_ERR_MEMORY;

  seg = r.w.ax;
  g_dsel = r.w.dx;
  phys = (unsigned long)seg * 16;

  /* Advance to next 128 KB boundary if buffer would cross one */
  if ((phys & ~0x1FFFFUL) != ((phys + FULL_BYTES - 1) & ~0x1FFFFUL))
    phys = (phys + 0x1FFFFUL) & ~0x1FFFFUL;

  g_phys = phys;
  g_buf = (short *)phys; /* flat model: linear == physical below 1 MB */

  /* --- Reset DSP --- */
  if (dsp_reset() != 0) {
    r.w.ax = 0x0101;
    r.w.dx = g_dsel;
    int386(0x31, &r, &r);
    return SB16_ERR_DSP;
  }

  /* --- Save + install protected-mode IRQ handler via DPMI --- */
  g_irq_vec = (unsigned char)(sb_irq < 8 ? 0x08 + sb_irq : 0x70 + (sb_irq - 8));

  r.w.ax = 0x0204;
  r.h.bl = g_irq_vec;
  int386(0x31, &r, &r);
  g_old_sel = r.w.cx;
  g_old_off = r.x.edx;

  segread(&sr);
  r.w.ax = 0x0205;
  r.h.bl = g_irq_vec;
  r.w.cx = sr.cs;
  r.x.edx = (unsigned long)irq_handler;
  int386(0x31, &r, &r);
  if (r.x.cflag) {
    dsp_reset();
    r.w.ax = 0x0101;
    r.w.dx = g_dsel;
    int386(0x31, &r, &r);
    return SB16_ERR_IRQ;
  }

  /* Enable IRQ in PIC mask */
  if (sb_irq < 8) {
    g_old_irq_mask = inp(PIC1_DATA);
    outp(PIC1_DATA, g_old_irq_mask & ~(1 << sb_irq));
  } else {
    g_old_irq_mask = inp(PIC2_DATA);
    outp(PIC2_DATA, g_old_irq_mask & ~(1 << (sb_irq - 8)));
  }

  /* --- Pre-fill both halves --- */
  fill(g_buf, SB16_HALF_SAMPLES);
  fill(g_buf + SB16_HALF_SAMPLES * 2, SB16_HALF_SAMPLES);
  g_half = 0;

  /* --- Program DMA channel 5 (full buffer, auto-init) --- */
  dma5_program(g_phys, FULL_BYTES);

  /* --- Start SB16 DSP --- */
  dsp_write(DSP_SET_OUT_RATE);
  dsp_write((unsigned char)((SB16_RATE >> 8) & 0xFF));
  dsp_write((unsigned char)(SB16_RATE & 0xFF));
  dsp_write(DSP_SPEAKER_ON);
  dsp_write(DSP_PLAY_16_AI);
  dsp_write(DSP_MODE_S16);
  /* DSP transfer count = words per half - 1 (2 words per stereo sample) */
  {
    unsigned short hcnt = (unsigned short)(SB16_HALF_SAMPLES * 2 - 1);
    dsp_write((unsigned char)(hcnt & 0xFF));
    dsp_write((unsigned char)(hcnt >> 8));
  }

  return SB16_OK;
}

void sb16_update(void) {
  short *ptr;
  int half;

  if (!g_need)
    return;

  half = g_half ^ 1; /* ISR toggled g_half already; fill the other one */
  ptr = g_buf + half * SB16_HALF_SAMPLES * 2;

  if (g_fill)
    g_fill(ptr, SB16_HALF_SAMPLES);
  g_need = 0;
}

void sb16_shutdown(void) {
  union REGS r;

  /* Stop DSP output */
  dsp_write(DSP_STOP_16);
  dsp_reset();

  /* Mask DMA channel 5 */
  outp(DMA5_MASK, 0x05);

  /* Restore PIC mask */
  if (sb_irq < 8)
    outp(PIC1_DATA, g_old_irq_mask);
  else
    outp(PIC2_DATA, g_old_irq_mask);

  /* Restore old protected-mode interrupt vector */
  r.w.ax = 0x0205;
  r.h.bl = g_irq_vec;
  r.w.cx = g_old_sel;
  r.x.edx = g_old_off;
  int386(0x31, &r, &r);

  /* Free DPMI conventional-memory block */
  r.w.ax = 0x0101;
  r.w.dx = g_dsel;
  int386(0x31, &r, &r);

  g_fill = NULL;
  g_buf = NULL;
}
