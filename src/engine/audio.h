/* audio.h - XM playback via libxmp-lite + SB16 */

#ifndef AUDIO_H
#define AUDIO_H

#include "../assets.h"
#include "sb16.h"

/*
 * Initialize SB16 audio output.
 * Returns 0 on success, negative on error.
 */
int audio_init(void);

/*
 * Load and start playing an XM module from the packed data file, seeked to
 * start_ms. The DMA buffer is flushed and primed with fresh audio so that
 * music at position start_ms becomes audible with no queued-buffer latency.
 * Returns 0 on success, negative on error.
 */
int audio_load(Asset asset, unsigned long start_ms);

/*
 * Must be called once per main-loop iteration to fill the audio buffer.
 */
#define audio_update() sb16_update()

/*
 * Seek to a position in the currently playing module. Flushes the DMA
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
