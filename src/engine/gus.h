/* gus.h - Gravis Ultrasound (GF1) digital output driver for DOS/32A */

#ifndef GUS_H
#define GUS_H

/* Ring buffer: 4096 mono 8-bit samples per voice @ 22050 Hz = 186 ms full cycle.
 * The driver refills a half at a time (2048 samples = 93 ms per half) by polling
 * the voice play position from gus_update(). */
#define GUS_RATE 22050
#define GUS_HALF_SAMPLES 2048

/* Return codes */
#define GUS_OK 0
#define GUS_ERR_NO_ULTRASND -1
#define GUS_ERR_RESET -2
#define GUS_ERR_MEMORY -3

/*
 * Callback invoked from gus_update() to fill one half-buffer.
 * Same signature as the SB16 driver's fill_fn.
 * buf    : destination, 16-bit signed stereo interleaved (L,R,L,R,...)
 * samples: number of stereo sample pairs to fill
 * The driver downmixes to 8-bit-per-channel mono internally.
 */
typedef void (*gus_fill_fn)(short *buf, int samples);

int gus_init(gus_fill_fn fill);
void gus_update(void); /* call once per main-loop iteration */
void gus_shutdown(void);

/* Stop voices, re-prime both halves, restart voices.
 * Returns timer_ms() at the moment voices begin consuming sample 0. */
unsigned long gus_restart(void);

#endif
