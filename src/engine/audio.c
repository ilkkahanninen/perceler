/*
 * audio.c - XM playback using libxmp-lite, dispatched to SB16 or GUS.
 *
 * At init time, the ULTRASND environment variable is checked. If present,
 * the GUS driver is attempted first; on failure (or if ULTRASND is absent)
 * the SB16 driver is used. Both drivers expose an identically-shaped fill
 * callback, so libxmp renders the same stereo-16 interleaved buffer in
 * either case — format conversion and any downmixing happens inside the
 * driver.
 */

#include "audio.h"

#include "data.h"
#include "gus.h"
#include "sb16.h"
#include "timer.h"
#include "xmp.h"

#include <stdlib.h>
#include <string.h>

typedef void (*fill_fn_t)(short *buf, int samples);

unsigned int audio_rate(void)
{
  static unsigned int cached = 0;
  if (cached == 0)
  {
    const char *s = getenv("PERCELER_RATE");
    unsigned int v = 22050;
    if (s && *s)
    {
      long parsed = strtol(s, NULL, 10);
      if (parsed >= 4000 && parsed <= 44100)
        v = (unsigned int)parsed;
    }
    cached = v;
  }
  return cached;
}

typedef struct
{
  int (*init)(fill_fn_t);
  void (*update)(void);
  void (*shutdown)(void);
  unsigned long (*restart)(void);
} audio_driver;

/* gus_fill_fn and sb16_fill_fn are both typedefs for void(*)(short*, int);
 * the casts only strip the different alias names from the init pointers. */
static const audio_driver DRV_GUS = {
    (int (*)(fill_fn_t))gus_init,
    gus_update,
    gus_shutdown,
    gus_restart};

static const audio_driver DRV_SB16 = {
    (int (*)(fill_fn_t))sb16_init,
    sb16_update,
    sb16_shutdown,
    sb16_restart};

static const audio_driver *g_drv = NULL;
static xmp_context g_ctx = NULL;
static int g_loaded = 0;
static int g_offline = 0;
static unsigned long g_audio_origin_ms = 0;

void audio_set_offline(int enabled)
{
  g_offline = enabled ? 1 : 0;
}

/* Called by the active driver → fill one half-buffer with decoded PCM */
static void fill_pcm(short *buf, int samples)
{
  int bytes = samples * 2 * 2; /* stereo * 16-bit */
  if (g_loaded)
  {
    if (xmp_play_buffer(g_ctx, buf, bytes, 1) < 0)
    {
      /* module ended or error — silence */
      memset(buf, 0, (unsigned)bytes);
    }
  }
  else
  {
    memset(buf, 0, (unsigned)bytes);
  }
}

int audio_init(void)
{
  int err;

  g_ctx = xmp_create_context();
  if (!g_ctx)
    return -1;

  if (g_offline)
    return 0; /* libxmp ready; no hardware driver */

  /* Prefer GUS if ULTRASND is set; fall back to SB16 on failure or
   * when ULTRASND is absent. */
  if (getenv("ULTRASND"))
  {
    g_drv = &DRV_GUS;
    err = g_drv->init(fill_pcm);
    if (err == 0)
      return 0;
  }

  g_drv = &DRV_SB16;
  err = g_drv->init(fill_pcm);
  if (err != 0)
  {
    xmp_free_context(g_ctx);
    g_ctx = NULL;
    g_drv = NULL;
    return err;
  }
  return 0;
}

int audio_load(Asset asset, unsigned long start_ms)
{
  void *buf;
  unsigned long t_restart;

  if (!g_ctx)
    return -1;
  if (!g_offline && !g_drv)
    return -1;

  if (g_loaded)
  {
    xmp_end_player(g_ctx);
    xmp_release_module(g_ctx);
    g_loaded = 0;
  }

  buf = data_read(asset);
  if (!buf)
    return -1;

  if (xmp_load_module_from_memory(g_ctx, buf, (long)asset.length) != 0)
  {
    free(buf);
    return -1;
  }
  free(buf);

  if (xmp_start_player(g_ctx, (int)audio_rate(), 0) != 0)
  {
    xmp_release_module(g_ctx);
    return -1;
  }

  xmp_set_player(g_ctx, XMP_PLAYER_DSP, 0);
  xmp_set_player(g_ctx, XMP_PLAYER_INTERP, XMP_INTERP_NEAREST);

  if (start_ms > 0)
    xmp_seek_time(g_ctx, (int)start_ms);

  g_loaded = 1;

  if (g_offline)
  {
    g_audio_origin_ms = 0; /* scene runner owns the virtual clock */
    return 0;
  }

  t_restart = g_drv->restart();
  g_audio_origin_ms = t_restart - start_ms;
  return 0;
}

void audio_seek(unsigned long ms)
{
  unsigned long t_restart;

  if (!g_loaded)
    return;

  xmp_seek_time(g_ctx, (int)ms);

  if (g_offline)
    return;
  if (!g_drv)
    return;

  t_restart = g_drv->restart();
  g_audio_origin_ms = t_restart - ms;
}

unsigned long audio_music_ms(void)
{
  return timer_ms() - g_audio_origin_ms;
}

void audio_update(void)
{
  if (g_drv)
    g_drv->update();
}

void audio_render_samples(short *buf, int samples)
{
  fill_pcm(buf, samples);
}

void audio_shutdown(void)
{
  if (g_drv)
  {
    g_drv->shutdown();
    g_drv = NULL;
  }

  if (g_ctx)
  {
    if (g_loaded)
    {
      xmp_end_player(g_ctx);
      xmp_release_module(g_ctx);
      g_loaded = 0;
    }
    xmp_free_context(g_ctx);
    g_ctx = NULL;
  }
}
