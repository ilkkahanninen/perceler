/* sb16.h - Sound Blaster 16 DMA output driver for DOS/32A */

#ifndef SB16_H
#define SB16_H

/* Half-buffer: 4096 stereo samples @ 44100 Hz ~= 93 ms per interrupt */
#define SB16_RATE 44100
#define SB16_HALF_SAMPLES 4096

/* Return codes */
#define SB16_OK 0
#define SB16_ERR_DSP -1
#define SB16_ERR_MEMORY -2
#define SB16_ERR_IRQ -3

/*
 * Callback invoked from audio_update() to fill one half-buffer.
 * buf    : destination (16-bit signed stereo interleaved)
 * samples: number of stereo sample pairs to fill
 */
typedef void (*sb16_fill_fn)(short *buf, int samples);

int sb16_init(sb16_fill_fn fill);
void sb16_update(void); /* call once per main-loop iteration */
void sb16_shutdown(void);

#endif
