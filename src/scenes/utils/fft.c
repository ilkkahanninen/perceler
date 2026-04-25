#include "fft.h"

#include <data.h>
#include <stdlib.h>

FFTTrack *fft_load(Asset asset)
{
  FFTTrack *t;
  unsigned char *buf = (unsigned char *)data_read(asset);
  if (!buf)
    return 0;
  t = (FFTTrack *)malloc(sizeof(FFTTrack));
  if (!t)
  {
    free(buf);
    return 0;
  }
  t->samples = buf;
  t->num_samples = asset.length;
  return t;
}

void fft_free(FFTTrack *track)
{
  if (!track)
    return;
  free(track->samples);
  free(track);
}

unsigned char fft_at(const FFTTrack *track, unsigned int frame)
{
  if (!track || (unsigned long)frame >= track->num_samples)
    return 0;
  return track->samples[frame];
}
