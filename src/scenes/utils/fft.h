#ifndef FFT_H
#define FFT_H

#include "../../assets.h"

/*
 * Per-frame FFT band-energy track loaded from an .fft asset.
 *
 * File format (produced by tools/wav2fft.py): a header-less stream of
 * unsigned bytes, one per frame at 60 fps. File length equals the
 * frame count; each byte is the band's normalised energy for that
 * frame in the range 0..255.
 */
typedef struct
{
  unsigned char *samples;   /* one byte per frame */
  unsigned long num_samples;
} FFTTrack;

/* Load an .fft asset. Returns 0 on error. */
FFTTrack *fft_load(Asset asset);

/* Free a track returned by fft_load(). Safe to pass NULL. */
void fft_free(FFTTrack *track);

/* Band energy at the given frame; returns 0 outside the track range. */
unsigned char fft_at(const FFTTrack *track, unsigned int frame);

/*
 * Peak-locked onset detector for a per-frame energy stream (FFT band
 * energy, audio level, or any other 0..255 series).
 *
 * The detector watches the running maximum of a rising slope. When
 * the signal turns downward, the previous frame was a peak; if that
 * peak cleared `floor` and at least `cooldown_frames` frames have
 * passed since the last detection, onset_step() returns 1 once.
 * Otherwise it returns 0.
 *
 * Effect:
 *   - One trigger per audible peak.
 *   - Small bumps in the decay tail are silently ignored if their
 *     own peak doesn't clear the floor.
 *   - Back-to-back peaks both fire as long as they're at least
 *     `cooldown_frames` apart.
 *
 * One-frame latency: the trigger fires on the falling edge after a
 * peak, so a peak at frame N is reported on frame N+1.
 */
typedef struct
{
  int floor;
  int cooldown_frames;
  int prev;
  int rising_peak;
  int cooldown;
} OnsetDetector;

/* Set config and zero the running state. Call once per scene init. */
void onset_init(OnsetDetector *d, int floor, int cooldown_frames);

/* Feed the next energy sample. Returns 1 on a detected onset, 0
 * otherwise. */
int onset_step(OnsetDetector *d, int energy);

#endif
