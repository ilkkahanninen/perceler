#ifndef CAPTURE_H
#define CAPTURE_H

/*
 * Deterministic offline render / capture sink.
 *
 * When enabled (PERCELER_CAPTURE=<prefix> in the environment) the
 * engine switches to an offline time base: a virtual clock advances
 * by exactly 1000/60 ms per frame, audio is rendered one frame's
 * worth per iteration via libxmp (not the hardware drivers), and
 * both video and audio are streamed to disk.
 *
 * Output files:
 *   <prefix>.RAW  Video. Magic "PRCL" then, per frame:
 *                   768   VGA DAC palette (R,G,B, values 0-63)
 *                   64000 pixel indices (320 × 200)
 *   <prefix>.WAV  Standard PCM WAV, stereo 16-bit signed at the rate
 *                 returned by audio_rate(). RIFF and data chunk sizes
 *                 are patched on close.
 *
 * Use tools/capture2video.py to encode the resulting RAW + WAV pair
 * into an MP4.
 */

/* Initialize capture. `prefix` is the DOS path stem (e.g. "CAPTURE");
 * the module appends ".RAW" and ".WAV". Returns 1 if both files were
 * opened, 0 otherwise. Safe to call with NULL or "". */
int capture_init(const char *prefix);

/* Non-zero when capture_init succeeded. */
int capture_enabled(void);

/* Append one frame's palette + pixel data to the video file. */
void capture_frame(const unsigned char *backbuffer);

/* Append `samples` stereo sample-pairs (4 bytes each, L/R 16-bit signed)
 * to the audio file. */
void capture_audio(const short *stereo_samples, int samples);

/* Close both files, patching WAV sizes. Safe to call if not enabled. */
void capture_shutdown(void);

#endif
