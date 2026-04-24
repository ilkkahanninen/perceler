/* audio.h - XM playback via libxmp-lite, dispatched to SB16 or GUS */

#ifndef AUDIO_H
#define AUDIO_H

#include "../assets.h"

/*
 * Effective output sample rate. Reads PERCELER_RATE from the environment
 * the first time it is called, clamps it to the range supported by both
 * drivers (4000..44100 Hz), and caches the result. Defaults to 22050 Hz
 * when PERCELER_RATE is unset or invalid.
 */
unsigned int audio_rate(void);

/*
 * Switch between real-time playback (default) and offline render mode.
 * In offline mode, no hardware driver is initialised; libxmp is still
 * set up so the scene runner can drive audio_render_samples() manually
 * to produce PCM alongside a deterministic frame loop.
 * Must be called before audio_init() to take effect.
 */
void audio_set_offline(int enabled);

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
 * In offline mode this is meaningless (the scene runner owns the clock).
 */
unsigned long audio_music_ms(void);

/*
 * Offline-only: render `samples` stereo sample-pairs from libxmp into
 * `buf` (interleaved signed 16-bit). No-op if not loaded. */
void audio_render_samples(short *buf, int samples);

/*
 * Stop playback and release all audio resources.
 */
void audio_shutdown(void);

#endif
