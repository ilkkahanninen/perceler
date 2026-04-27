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

void onset_init(OnsetDetector *d, int floor, int cooldown_frames)
{
  d->floor = floor;
  d->cooldown_frames = cooldown_frames;
  d->prev = 0;
  d->rising_peak = 0;
  d->cooldown = 0;
}

int onset_step(OnsetDetector *d, int energy)
{
  int fired = 0;

  if (d->cooldown > 0)
    d->cooldown--;

  if (energy > d->prev)
  {
    if (energy > d->rising_peak)
      d->rising_peak = energy;
  }
  else if (energy < d->prev && d->rising_peak > 0)
  {
    if (d->rising_peak > d->floor && d->cooldown == 0)
    {
      fired = 1;
      d->cooldown = d->cooldown_frames;
    }
    d->rising_peak = 0;
  }
  d->prev = energy;
  return fired;
}
