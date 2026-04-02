/* audio.h - XM playback via libxmp-lite + SB16 */

#ifndef AUDIO_H
#define AUDIO_H

/*
 * Initialize SB16 audio output.
 * Returns 0 on success, negative on error.
 */
int  audio_init(void);

/*
 * Load and immediately start playing an XM (or MOD/S3M/IT) file.
 * path: DOS path to the module file (e.g. "music.xm")
 * Returns 0 on success, negative on error.
 */
int  audio_load(const char *path);

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
