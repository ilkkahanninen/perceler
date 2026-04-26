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

#endif
