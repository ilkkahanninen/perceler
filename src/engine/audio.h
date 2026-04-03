/* audio.h - XM playback via libxmp-lite + SB16 */

#ifndef AUDIO_H
#define AUDIO_H

#include "../assets.h"

/*
 * Initialize SB16 audio output.
 * Returns 0 on success, negative on error.
 */
int audio_init(void);

/*
 * Load and immediately start playing an XM module from the packed data file.
 * asset: position and size within demo.dat.
 * Returns 0 on success, negative on error.
 */
int audio_load(Asset asset);

/*
 * Must be called once per main-loop iteration to fill the audio buffer.
 */
void audio_update(void);

/*
 * Seek to a position in the currently playing module.
 * ms: milliseconds from the start of the module.
 */
void audio_seek(unsigned long ms);

/*
 * Stop playback and release all audio resources.
 */
void audio_shutdown(void);

#endif
