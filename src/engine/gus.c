/*
 * gus.c - Gravis Ultrasound (GF1) driver for DOS/32A (Open Watcom)
 *
 * Streams stereo PCM by allocating a small ring buffer in GF1 onboard DRAM
 * and running two voices (one per channel, hard-panned L/R) in loop mode
 * over it. Refills are scheduled by polling the voice play position from
 * the main loop: when a voice crosses the halfway boundary, the half it
 * just finished is refilled via GF1 DRAM DMA upload. No GUS interrupt
 * handler is installed — the rollover IRQ only fires once per full loop,
 * which is too coarse for double-buffered streaming.
 *
 * Samples in GF1 DRAM are 8-bit. libxmp renders signed 16-bit stereo;
 * the driver downsamples to signed 8-bit and the GF1 DMA engine flips
 * the MSB on upload (DMA ctrl bit 7 — "flip sign"), so the final storage
 * in GUS RAM is unsigned offset-binary, which is what GF1 voices expect
 * when the "16-bit" voice-control bit is clear.
 *
 * Configuration is read from ULTRASND:
 *   ULTRASND=port,dma1,dma2,irq1,irq2
 * We use only dma1 (primary DMA). IRQs are not used.
 *
 * The card is assumed to already have its IRQ/DMA resources latched by
 * the resident Ultrasound driver (ULTRINIT/ULTRMID setup in AUTOEXEC);
 * this driver only touches GF1 core registers.
 *
 * --- Register summary (written to GUS_REG_INDEX = base+0x103, then data
 * via GUS_DATA_LO/HI at base+0x104/0x105) ---
 *
 *   0x00  Voice Control   (bits: see VC_* below)
 *   0x01  Frequency       (16-bit: upper=integer, val>>1 is increment/512
 *                          relative to base sample rate)
 *   0x02/0x03  Start addr  MSW / LSW   (20-bit byte addr, see VOICE_ADDR_*)
 *   0x04/0x05  End addr    MSW / LSW
 *   0x06  Ramp Rate
 *   0x07/0x08  Ramp Start / End
 *   0x09  Current Volume  (16-bit; 0x0000 = silent, 0xFFF0 ≈ max)
 *   0x0A/0x0B  Current addr MSW / LSW   (read for play position)
 *   0x0C  Pan             (0 = hard left, 15 = hard right, 7/8 = center)
 *   0x0D  Ramp Control    (bit 0 = stopped; same shape as Voice Control)
 *   0x0E  Active Voices   (upper bits 0xC0, lower 5 bits = count-1)
 *   0x41  DMA Control     (DC_* bits below)
 *   0x42  DMA Address     (GF1 RAM dest >> 4, 16-byte-aligned)
 *   0x43/0x44  DRAM peek/poke addr (not used here — we use DMA only)
 *   0x4C  Reset / Enable  (RS_* bits below)
 *
 */

#include "gus.h"

#include "audio.h"
#include "timer.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Configuration (populated from ULTRASND, with ULTRADIR fallback)     */
/* ------------------------------------------------------------------ */
static unsigned short gus_base = 0x220;
static unsigned char gus_dma = 1; /* primary DMA channel used for GF1 RAM upload */

/* Port offsets (macros so they track gus_base at runtime) */
#define GUS_RESET_PORT ((unsigned short)(gus_base + 0x006)) /* 2x6: reset (write), IRQ status (read) */
#define GUS_DMA_IRQ ((unsigned short)(gus_base + 0x00B))    /* 2xB: DMA/IRQ select — not touched here */
#define GUS_MIX_CTRL ((unsigned short)(gus_base + 0x100))   /* 3x0 */
#define GUS_VOICE_SEL ((unsigned short)(gus_base + 0x102))  /* 3x2 */
#define GUS_REG_INDEX ((unsigned short)(gus_base + 0x103))  /* 3x3 */
#define GUS_DATA_LO ((unsigned short)(gus_base + 0x104))    /* 3x4 */
#define GUS_DATA_HI ((unsigned short)(gus_base + 0x105))    /* 3x5 */

/* Global register indices (written to GUS_REG_INDEX) */
#define GF1_VOICE_CTRL 0x00
#define GF1_FREQ 0x01
#define GF1_START_HI 0x02
#define GF1_START_LO 0x03
#define GF1_END_HI 0x04
#define GF1_END_LO 0x05
#define GF1_CURR_VOL 0x09
#define GF1_CURR_HI 0x0A
#define GF1_CURR_LO 0x0B
#define GF1_PAN 0x0C
#define GF1_RAMP_CTRL 0x0D
#define GF1_ACTIVE_VOICES 0x0E
#define GF1_DMA_CTRL 0x41
#define GF1_DMA_ADDR 0x42
#define GF1_RESET 0x4C
/* Read-variant indices are index | 0x80 (e.g. 0x80 for voice control read) */

/* Voice control bits (GF1_VOICE_CTRL) */
#define VC_STOPPED 0x01    /* 1 = voice halted                    */
#define VC_STOP_REQ 0x02   /* write 1 to request stop             */
#define VC_16BIT 0x04      /* 1 = 16-bit sample format in DRAM    */
#define VC_LOOP 0x08       /* 1 = loop enabled                    */
#define VC_BIDIR 0x10      /* 1 = bidirectional loop              */
#define VC_IRQ_AT_END 0x20 /* 1 = fire voice IRQ on loop end      */
#define VC_REVERSE 0x40    /* 1 = reverse playback direction      */

/* DMA control bits (GF1_DMA_CTRL) */
#define DC_ENABLE 0x01   /* 1 = start DMA                        */
#define DC_DIR_READ 0x02 /* 1 = GF1→ISA download; 0 = upload     */
#define DC_16BIT_CH 0x04 /* 1 = data path is 16-bit (ch 5-7)     */
/* bits 3..4 = rate divisor (00 = fastest ~650 kHz)               */
#define DC_IRQ_EN 0x20    /* DMA-complete IRQ enable              */
#define DC_TC 0x40        /* read: 1 = DMA terminal count reached */
#define DC_FLIP_SIGN 0x80 /* 1 = invert MSB during upload         */

/* Reset register bits (GF1_RESET) */
#define RS_MASTER 0x01     /* 1 = GF1 enabled (0 = held in reset) */
#define RS_DAC_ENABLE 0x02 /* 1 = DAC output enabled              */
#define RS_IRQ_ENABLE 0x04 /* 1 = GF1 IRQs enabled                */

/* ------------------------------------------------------------------ */
/* Derived buffer sizing                                                */
/* ------------------------------------------------------------------ */
#define HALF_SAMPLES GUS_HALF_SAMPLES
#define FULL_SAMPLES (HALF_SAMPLES * 2)
#define BUF_BYTES_PER_CH FULL_SAMPLES                        /* 8-bit mono, one byte per sample */
#define STAGE_BYTES_16 ((unsigned long)HALF_SAMPLES * 2 * 2) /* stereo 16-bit for one half */

/* GF1 DRAM layout: two channels, with a safety gap between them.
 * 16 KiB per channel is overkill for 4 KiB of samples but keeps each
 * buffer far from any boundary the GF1's addressing quirks care about. */
#define GF1_BUF_L 0x00000UL
#define GF1_BUF_R 0x04000UL

#define VOICE_L 0
#define VOICE_R 1

/* ------------------------------------------------------------------ */
/* Voice address encoding                                               */
/* ------------------------------------------------------------------ */
/*
 * GF1 voice addresses are 20-bit byte addresses encoded across MSW + LSW:
 *   MSW (reg 0x02/0x04/0x0A) = (byte_addr >> 7) & 0x1FFF
 *   LSW (reg 0x03/0x05/0x0B) = (byte_addr & 0x7F) << 9
 * Low 9 bits of LSW are fractional position and are zero for starts/ends.
 */
#define VOICE_ADDR_MSW(a) ((unsigned short)(((unsigned long)(a) >> 7) & 0x1FFFUL))
#define VOICE_ADDR_LSW(a) ((unsigned short)(((unsigned long)(a) & 0x7FUL) << 9))

/* ------------------------------------------------------------------ */
/* Driver state                                                         */
/* ------------------------------------------------------------------ */
static gus_fill_fn g_fill = NULL;
static int g_running = 0;

/* Staging in conventional memory: one half's worth of stereo PCM, then
 * de-interleaved into L/R byte buffers for DMA. All allocated via DPMI
 * 0x0100 so the physical address is known and DMA-safe. */
static short *g_stage16 = NULL;
static unsigned char *g_stageL = NULL;
static unsigned char *g_stageR = NULL;
static unsigned long g_stage16_phys = 0; /* unused (CPU-only) but kept for symmetry */
static unsigned long g_stageL_phys = 0;
static unsigned long g_stageR_phys = 0;
static unsigned short g_dsel_stage16 = 0;
static unsigned short g_dsel_stage_mono = 0;

/* Tracks which half was most recently refilled (0 or 1).
 * Refill the opposite half when the voice is playing from the same half
 * as the last refill — i.e. the voice has just crossed into that half
 * and the other side is now safe to overwrite. */
static int g_last_filled_half = -1;

/* ------------------------------------------------------------------ */
/* Register I/O helpers                                                 */
/* ------------------------------------------------------------------ */
static void gf1_write8(unsigned char reg, unsigned char val)
{
  outp(GUS_REG_INDEX, reg);
  outp(GUS_DATA_HI, val);
}

static void gf1_write16(unsigned char reg, unsigned short val)
{
  outp(GUS_REG_INDEX, reg);
  outpw(GUS_DATA_LO, val);
}

static unsigned char gf1_read8(unsigned char reg)
{
  outp(GUS_REG_INDEX, reg);
  return inp(GUS_DATA_HI);
}

static unsigned short gf1_read16(unsigned char reg)
{
  outp(GUS_REG_INDEX, reg);
  return inpw(GUS_DATA_LO);
}

static void select_voice(unsigned char v)
{
  outp(GUS_VOICE_SEL, (unsigned char)(v & 0x1F));
}

static void io_delay(int n)
{
  int i;
  for (i = 0; i < n; i++)
    inp(0x80);
}

/* ------------------------------------------------------------------ */
/* ULTRASND env parser                                                  */
/* ------------------------------------------------------------------ */
/* Format: ULTRASND=<port_hex>,<dma1>,<dma2>,<irq1>,<irq2>               */
static int parse_ultrasnd(void)
{
  char *p = getenv("ULTRASND");
  char *q;
  long v;
  if (!p)
    return -1;
  v = strtol(p, &q, 16);
  if (*q != ',' || v <= 0)
    return -1;
  gus_base = (unsigned short)v;
  p = q + 1;
  v = strtol(p, &q, 10);
  if (*q != ',')
    return -1;
  gus_dma = (unsigned char)v;
  /* remaining fields (dma2, irq1, irq2) ignored */
  return 0;
}

/* ------------------------------------------------------------------ */
/* GF1 reset sequence                                                   */
/* ------------------------------------------------------------------ */
/*
 *   1. Write 0x00 to reset (chip held in reset, DAC off, IRQ off).
 *   2. Write RS_MASTER (chip online).
 *   3. Silence and stop every one of the 32 voices.
 *   4. Write RS_MASTER | RS_DAC_ENABLE | RS_IRQ_ENABLE to go live.
 *   5. Set active-voice count — determines the device sample rate.
 */
static int gf1_reset_and_init(void)
{
  int i;
  unsigned char status;

  gf1_write8(GF1_RESET, 0x00);
  io_delay(300);
  gf1_write8(GF1_RESET, RS_MASTER);
  io_delay(300);

  /* Probe: the GF1 reset register's master bit should read back as 1. */
  status = gf1_read8(GF1_RESET | 0x80);
  if ((status & RS_MASTER) == 0)
    return -1;

  /* Silence/stop every voice (safe initial state). */
  for (i = 0; i < 32; i++)
  {
    select_voice((unsigned char)i);
    gf1_write8(GF1_VOICE_CTRL, VC_STOPPED | VC_STOP_REQ);
    gf1_write8(GF1_RAMP_CTRL, VC_STOPPED | VC_STOP_REQ);
    gf1_write16(GF1_CURR_VOL, 0x0000);
    gf1_write8(GF1_PAN, 0x07);
  }

  /* Drain pending voice IRQs (read-to-ack). */
  for (i = 0; i < 32; i++)
    (void)gf1_read8(0x8F);
  (void)inp(GUS_RESET_PORT);

  /* Full enable: chip + DAC + IRQ logic. */
  gf1_write8(GF1_RESET, RS_MASTER | RS_DAC_ENABLE | RS_IRQ_ENABLE);
  io_delay(300);

  /* 14 active voices → ~44100 Hz device sample rate, the standard choice. */
  gf1_write8(GF1_ACTIVE_VOICES, (unsigned char)(0xC0 | (14 - 1)));

  /* Mix control 0x0B: line-in muted, mic muted, line-out enabled, latches on.
   * DOSBox-X uses this as the post-reset default and it matches GUS SDK docs. */
  outp(GUS_MIX_CTRL, 0x0B);

  return 0;
}

/* ------------------------------------------------------------------ */
/* System-DMA programmer (ISA 8237A)                                    */
/* ------------------------------------------------------------------ */
/*
 * Programs the ISA DMA controller for a single-transfer memory-read
 * (memory → GUS) from src_phys of `bytes` length, on channel gus_dma.
 * Does not wait for completion — that is handled on the GF1 side.
 */
static const unsigned char g_dma_page_port[8] = {
    0x87, 0x83, 0x81, 0x82, 0x00, 0x8B, 0x89, 0x8A};

static void program_system_dma(unsigned long src_phys, unsigned long bytes)
{
  unsigned char page, mask_val, mode_val;
  unsigned short addr_port, cnt_port, mask_port, mode_port, flip_port;
  unsigned short waddr, wcnt;

  if (gus_dma < 4)
  {
    /* 8-bit controller (ch 0-3) — address is byte-level */
    mask_port = 0x0A;
    mode_port = 0x0B;
    flip_port = 0x0C;
    addr_port = (unsigned short)(gus_dma * 2);
    cnt_port = (unsigned short)(gus_dma * 2 + 1);
    mask_val = (unsigned char)(gus_dma | 0x04);
    mode_val = (unsigned char)(0x48 | gus_dma); /* single, mem-read */
    waddr = (unsigned short)(src_phys & 0xFFFF);
    wcnt = (unsigned short)(bytes - 1);
  }
  else
  {
    /* 16-bit controller (ch 5-7) — address is word-level, count is words */
    unsigned char ch3 = (unsigned char)(gus_dma & 3);
    mask_port = 0xD4;
    mode_port = 0xD6;
    flip_port = 0xD8;
    addr_port = (unsigned short)(0xC0 + (ch3 * 4));
    cnt_port = (unsigned short)(addr_port + 2);
    mask_val = (unsigned char)(ch3 | 0x04);
    mode_val = (unsigned char)(0x48 | ch3);
    waddr = (unsigned short)((src_phys >> 1) & 0xFFFF);
    wcnt = (unsigned short)((bytes / 2) - 1);
  }
  page = (unsigned char)((src_phys >> 16) & 0xFF);

  outp(mask_port, mask_val); /* mask channel during programming */
  outp(flip_port, 0x00);     /* reset flip-flop                 */
  outp(mode_port, mode_val); /* single + memory-read            */
  outp(addr_port, (unsigned char)(waddr & 0xFF));
  outp(addr_port, (unsigned char)((waddr >> 8) & 0xFF));
  outp(g_dma_page_port[gus_dma], page);
  outp(cnt_port, (unsigned char)(wcnt & 0xFF));
  outp(cnt_port, (unsigned char)((wcnt >> 8) & 0xFF));
  outp(mask_port, (unsigned char)(gus_dma & 3)); /* unmask → DMA can fire */
}

static void mask_system_dma(void)
{
  if (gus_dma < 4)
    outp(0x0A, (unsigned char)(gus_dma | 0x04));
  else
    outp(0xD4, (unsigned char)((gus_dma & 3) | 0x04));
}

/* ------------------------------------------------------------------ */
/* GF1 DRAM upload                                                      */
/* ------------------------------------------------------------------ */
/*
 * Upload `bytes` from system RAM (src_phys) to GF1 DRAM at gf1_addr.
 * Data is treated as signed 8-bit; the flip-sign bit converts it to
 * unsigned offset-binary in GUS memory.
 *
 * Blocking: polls GF1 DMA TC bit until transfer completes. Typical
 * duration for HALF_SAMPLES (2048) bytes at 650 kHz ≈ 3 ms.
 */
static void dma_upload(unsigned long src_phys, unsigned long gf1_addr, unsigned long bytes)
{
  unsigned char ctrl;
  int timeout;

  program_system_dma(src_phys, bytes);

  /* GF1 DMA destination — 16-byte units */
  gf1_write16(GF1_DMA_ADDR, (unsigned short)((gf1_addr >> 4) & 0xFFFF));

  /* Start upload: enable, direction=upload (bit 1 clear), flip sign.
   * Set 16-bit-channel flag if using a 16-bit DMA channel (5-7). */
  ctrl = DC_ENABLE | DC_FLIP_SIGN;
  if (gus_dma >= 4)
    ctrl |= DC_16BIT_CH;
  gf1_write8(GF1_DMA_CTRL, ctrl);

  /* Poll for terminal count. Cap iterations so a misconfigured card
   * doesn't hang the whole demo. */
  for (timeout = 0; timeout < 200000; timeout++)
  {
    unsigned char s = gf1_read8(GF1_DMA_CTRL | 0x80);
    if (s & DC_TC)
      break;
  }

  gf1_write8(GF1_DMA_CTRL, 0x00);
  mask_system_dma();
}

/* ------------------------------------------------------------------ */
/* Voice programming                                                    */
/* ------------------------------------------------------------------ */
/*
 * Configure a voice for looping 8-bit playback over its ring buffer,
 * with the play position starting at sample 0 and voice halted
 * (caller will un-halt when both voices are ready).
 */
static void voice_program(unsigned char v, unsigned long buf_addr, unsigned char pan)
{
  unsigned long start = buf_addr;
  unsigned long end = buf_addr + BUF_BYTES_PER_CH - 1;

  select_voice(v);

  /* Halt voice before reprogramming addresses. */
  gf1_write8(GF1_VOICE_CTRL, VC_STOPPED | VC_STOP_REQ);
  gf1_write8(GF1_RAMP_CTRL, VC_STOPPED | VC_STOP_REQ);
  io_delay(10);

  /* Frequency: at 14 active voices the GF1 base rate is 44100 Hz. The
   * register value is desired_rate * 1024 / base_rate (1024 = 1.0), so
   * 22050 Hz → 0x0200, 44100 Hz → 0x0400. */
  gf1_write16(GF1_FREQ,
              (unsigned short)((unsigned long)audio_rate() * 1024UL / 44100UL));

  gf1_write16(GF1_START_HI, VOICE_ADDR_MSW(start));
  gf1_write16(GF1_START_LO, VOICE_ADDR_LSW(start));
  gf1_write16(GF1_END_HI, VOICE_ADDR_MSW(end));
  gf1_write16(GF1_END_LO, VOICE_ADDR_LSW(end));
  gf1_write16(GF1_CURR_HI, VOICE_ADDR_MSW(start));
  gf1_write16(GF1_CURR_LO, VOICE_ADDR_LSW(start));

  gf1_write8(GF1_PAN, pan);

  /* Max volume. GF1 volume is a 16-bit exponential/mantissa thing;
   * 0xFFF0 is the published "full scale" value. */
  gf1_write16(GF1_CURR_VOL, 0xFFF0);
}

static void voices_start(void)
{
  /* Write loop-enabled, not-stopped voice control back-to-back; any
   * drift between L and R start is sub-microsecond and inaudible. */
  select_voice(VOICE_L);
  gf1_write8(GF1_VOICE_CTRL, VC_LOOP);
  gf1_write8(GF1_RAMP_CTRL, VC_STOPPED | VC_STOP_REQ); /* no envelope */
  select_voice(VOICE_R);
  gf1_write8(GF1_VOICE_CTRL, VC_LOOP);
  gf1_write8(GF1_RAMP_CTRL, VC_STOPPED | VC_STOP_REQ);
}

static void voices_stop(void)
{
  select_voice(VOICE_L);
  gf1_write8(GF1_VOICE_CTRL, VC_STOPPED | VC_STOP_REQ);
  select_voice(VOICE_R);
  gf1_write8(GF1_VOICE_CTRL, VC_STOPPED | VC_STOP_REQ);
}

/* Read current play position (byte offset within the voice's ring). */
static unsigned long voice_play_offset(unsigned char v, unsigned long base_addr)
{
  unsigned long cur;
  unsigned short msw, lsw;
  select_voice(v);
  msw = gf1_read16(GF1_CURR_HI | 0x80);
  lsw = gf1_read16(GF1_CURR_LO | 0x80);
  cur = ((unsigned long)(msw & 0x1FFF) << 7) | ((unsigned long)(lsw & 0xFE00) >> 9);
  if (cur < base_addr)
    return 0; /* shouldn't happen unless voice has been stopped or rolled */
  return cur - base_addr;
}

/* ------------------------------------------------------------------ */
/* Callback + conversion                                                */
/* ------------------------------------------------------------------ */
/*
 * Call the user's fill function to render one half-buffer of 16-bit
 * stereo PCM, then de-interleave and downsample into g_stageL/R as
 * signed 8-bit.
 */
static void render_half(void)
{
  int i;
  short *src;

  g_fill(g_stage16, HALF_SAMPLES);

  src = g_stage16;
  for (i = 0; i < HALF_SAMPLES; i++)
  {
    /* 16-bit signed → 8-bit signed by arithmetic shift. Rounding isn't
     * worth the branch at this bit depth. */
    g_stageL[i] = (unsigned char)((signed char)(src[0] >> 8));
    g_stageR[i] = (unsigned char)((signed char)(src[1] >> 8));
    src += 2;
  }
}

/* Upload the rendered staging buffers to one half of each voice's ring. */
static void upload_half(int half)
{
  unsigned long offset = (half == 0) ? 0 : HALF_SAMPLES;
  dma_upload(g_stageL_phys, GF1_BUF_L + offset, HALF_SAMPLES);
  dma_upload(g_stageR_phys, GF1_BUF_R + offset, HALF_SAMPLES);
}

/* ------------------------------------------------------------------ */
/* DPMI conventional-memory allocator                                   */
/* ------------------------------------------------------------------ */
static int alloc_conv(unsigned long bytes, void **ptr, unsigned long *phys, unsigned short *dsel)
{
  union REGS r;
  unsigned short seg;
  unsigned long paddr;

  r.w.ax = 0x0100;
  r.w.bx = (unsigned short)((bytes + 15) / 16);
  int386(0x31, &r, &r);
  if (r.x.cflag)
    return -1;

  seg = r.w.ax;
  *dsel = r.w.dx;
  paddr = (unsigned long)seg * 16;

  *phys = paddr;
  *ptr = (void *)paddr; /* flat model: linear == physical below 1 MB */
  return 0;
}

static void free_conv(unsigned short dsel)
{
  union REGS r;
  if (!dsel)
    return;
  r.w.ax = 0x0101;
  r.w.dx = dsel;
  int386(0x31, &r, &r);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
int gus_init(gus_fill_fn fill)
{
  void *p16, *pmono;
  unsigned long pmono_phys;

  if (parse_ultrasnd() != 0)
    return GUS_ERR_NO_ULTRASND;

  g_fill = fill;

  /* 16-bit stereo staging: one half's worth */
  if (alloc_conv(STAGE_BYTES_16, &p16, &g_stage16_phys, &g_dsel_stage16) != 0)
    return GUS_ERR_MEMORY;
  g_stage16 = (short *)p16;

  /* Two contiguous mono buffers (L then R), one half each, for DMA source.
   * Single allocation so both lie in the same 64 KB DMA page. */
  if (alloc_conv(HALF_SAMPLES * 2UL, &pmono, &pmono_phys, &g_dsel_stage_mono) != 0)
  {
    free_conv(g_dsel_stage16);
    g_dsel_stage16 = 0;
    return GUS_ERR_MEMORY;
  }
  g_stageL = (unsigned char *)pmono;
  g_stageR = g_stageL + HALF_SAMPLES;
  g_stageL_phys = pmono_phys;
  g_stageR_phys = pmono_phys + HALF_SAMPLES;

  if (gf1_reset_and_init() != 0)
  {
    free_conv(g_dsel_stage_mono);
    free_conv(g_dsel_stage16);
    g_dsel_stage_mono = g_dsel_stage16 = 0;
    return GUS_ERR_RESET;
  }

  /* Program both voices over their ring buffers (halted for now). */
  voice_program(VOICE_L, GF1_BUF_L, 0x00); /* hard left  */
  voice_program(VOICE_R, GF1_BUF_R, 0x0F); /* hard right */

  g_running = 0;
  g_last_filled_half = -1;
  return GUS_OK;
}

/* Pre-fill both halves of the GF1 RAM ring, then start both voices.
 * Returns timer_ms() at the instant playback begins. */
static unsigned long prime_and_start(void)
{
  unsigned long t_start;

  render_half();
  upload_half(0);
  render_half();
  upload_half(1);

  /* Voice just started → we're about to play from half 0, so the most
   * recently "safe" refill target will be half 0 after we cross into 1. */
  g_last_filled_half = 1;

  voices_start();
  t_start = timer_ms();
  g_running = 1;
  return t_start;
}

void gus_update(void)
{
  unsigned long pos;
  int current_half, refill_half;

  if (!g_running)
    return;

  /* Poll left voice's position (right is kept in lockstep and drifts by
   * at most a couple of samples). */
  pos = voice_play_offset(VOICE_L, GF1_BUF_L);
  current_half = (pos >= HALF_SAMPLES) ? 1 : 0;

  /* Refill the half the voice is NOT playing — but only once per crossing,
   * so skip if we already filled the opposite half during this pass. */
  refill_half = current_half ^ 1;
  if (g_last_filled_half == refill_half)
    return;

  render_half();
  upload_half(refill_half);
  g_last_filled_half = refill_half;
}

void gus_shutdown(void)
{
  if (g_running)
  {
    voices_stop();
    g_running = 0;
  }
  /* Park the chip in a quiet state. */
  gf1_write8(GF1_RESET, RS_MASTER);

  free_conv(g_dsel_stage_mono);
  free_conv(g_dsel_stage16);
  g_dsel_stage_mono = g_dsel_stage16 = 0;
  g_stage16 = NULL;
  g_stageL = g_stageR = NULL;
  g_fill = NULL;
}

unsigned long gus_restart(void)
{
  if (g_running)
  {
    voices_stop();
    g_running = 0;
  }
  g_last_filled_half = -1;
  return prime_and_start();
}
