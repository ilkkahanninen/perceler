/* audio.h - XM playback via libxmp-lite, dispatched to SB16 or GUS */

#ifndef AUDIO_H
#define AUDIO_H

#include "../assets.h"

/* Mix/output rate. Must match both SB16_RATE and GUS_RATE; audio.c
 * enforces this at compile time. */
#define AUDIO_RATE 22050

/*
 * Initialize the audio subsystem. Picks a driver at runtime: if the
 * ULTRASND environment variable is set, the GUS driver is attempted
 * first; on failure (or if ULTRASND is absent), the SB16 driver is
 * used. Returns 0 on success, negative on error.
 */
int audio_init(void);

/*
 * Load and start playing an XM module from the packed data file, seeked to
 * start_ms. The output ring is flushed and primed with fresh audio so that
 * music at position start_ms becomes audible with no queued-buffer latency.
 * Returns 0 on success, negative on error.
 */
int audio_load(Asset asset, unsigned long start_ms);

/*
 * Must be called once per main-loop iteration to refill the output buffer.
 */
void audio_update(void);

/*
 * Seek to a position in the currently playing module. Flushes the output
 * buffer so music at ms becomes audible immediately.
 */
void audio_seek(unsigned long ms);

/*
 * Wall-clock milliseconds of music that has actually been heard since the
 * most recent load/seek. Callers use this as the master clock for visual
 * sync — it is never ahead of what the speakers are outputting.
 */
unsigned long audio_music_ms(void);

/*
 * Stop playback and release all audio resources.
 */
void audio_shutdown(void);

#endif
