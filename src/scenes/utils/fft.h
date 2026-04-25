#ifndef FFT_H
#define FFT_H

#include "../../assets.h"

/*
 * Per-frame FFT band-energy track loaded from an .fft asset.
 *
 * File format (produced by tools/wav2fft.py): a header-less stream
 * of unsigned bytes, one per frame at 60 fps. File length equals
 * the frame count, and each byte is the band's normalised energy
 * for that frame in the range 0..255.
 *
 * Usage:
 *   FFTTrack *kick = fft_load(ASSET_SONG_KICK_FFT);
 *   ...
 *   unsigned char e = fft_at(kick, ctx->timeline_frame);
 *   ...
 *   fft_free(kick);
 */
typedef struct
{
  unsigned char *samples;
  unsigned long num_samples;
} FFTTrack;

FFTTrack *fft_load(Asset asset);
void fft_free(FFTTrack *track);

/* Energy at the given frame; returns 0 outside the track. */
unsigned char fft_at(const FFTTrack *track, unsigned int frame);

#endif
