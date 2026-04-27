/*
 * scene.c - Timeline-driven scene runner
 *
 * Supports jumping between scenes with left/right arrow keys.
 * Music position is synchronized when jumping.
 */

#include "scene.h"

#include "audio.h"
#include "capture.h"
#include "keyboard.h"
#include "utils/mem.h"
#include "../utils/timing.h"
#include "timer.h"
#include "vga.h"

#include <stdlib.h>
#include <string.h>

#define PROG_X 40
#define PROG_Y 96
#define PROG_W 240
#define PROG_H 8

static void draw_progress(int loaded, int total)
{
  int filled_w, y, x;
  volatile unsigned char *row;

  if (total <= 0)
    return;

  filled_w = (PROG_W * loaded) / total;

  for (y = 0; y < PROG_H; y++)
  {
    row = VGA_MEM + (PROG_Y + y) * VGA_WIDTH + PROG_X;
    for (x = 0; x < PROG_W; x++)
      row[x] = (x < filled_w) ? 15 : 8;
  }
}

int timeline_init(TimelineEntry *tl)
{
  int n = 0;
  unsigned long offset = 0;
  while (tl[n].scene)
  {
    tl[n].music_offset_ms = offset;
    offset += tl[n].duration_ms;
    n++;
  }
  return n;
}

int timeline_select(int argc, char *argv[], const TimelineEntry *source,
                    int source_len, TimelineEntry *dest, int max)
{
  int i, n = 0;
  for (i = 1; i < argc && n < max; i++)
  {
    int idx = atoi(argv[i]);
    if (idx >= 0 && idx < source_len)
      dest[n++] = source[idx];
  }
  dest[n].scene = 0;
  dest[n].duration_ms = 0;
  dest[n].music_offset_ms = 0;
  return n;
}

/* Count non-sentinel entries, run every setup, display the setup progress
 * bar, and return the scene count. Shared by real-time and offline paths. */
static int prepare_timeline(const TimelineEntry *timeline)
{
  int number_of_scenes;
  int i;

  for (number_of_scenes = 0; timeline[number_of_scenes].scene != 0;
       number_of_scenes++)
    ;
  if (number_of_scenes == 0)
    return 0;

  draw_progress(0, number_of_scenes);
  for (i = 0; i < number_of_scenes; i++)
  {
    timeline[i].scene->setup();
    draw_progress(i + 1, number_of_scenes);
  }
  return number_of_scenes;
}

static void shutdown_timeline(const TimelineEntry *timeline, int n)
{
  int i;
  for (i = 0; i < n; i++)
    timeline[i].scene->shutdown();
}

static void run_timeline_realtime(const TimelineEntry *timeline,
                                  const Asset *song, int loop,
                                  TimelineStats *stats, int number_of_scenes,
                                  unsigned char *backbuffer)
{
  unsigned long elapsed, run_start;
  unsigned long frames = 0;
  int current_scene_idx, need_init, need_seek;
  const TimelineEntry *current_scene;

  current_scene_idx = 0;
  need_init = 1;
  need_seek = 0;
  current_scene = &timeline[0];

  if (song)
    audio_load(*song, current_scene->music_offset_ms);

  run_start = timer_ms();

  while (!key_down(KEY_ESC))
  {
    audio_update();

    {
      RenderContext ctx;
      elapsed = audio_music_ms() - current_scene->music_offset_ms;
      ctx.backbuffer = backbuffer;
      ctx.ms = elapsed;
      ctx.timeline_ms = current_scene->music_offset_ms + elapsed;
      ctx.frame = (unsigned int)MS_TO_FRAME(ctx.ms);
      ctx.timeline_frame = (unsigned int)MS_TO_FRAME(ctx.timeline_ms);

      if (need_init)
      {
        current_scene->scene->init(&ctx);
        need_init = 0;
      }
      current_scene->scene->render(&ctx);
    }

    audio_update();

    frames++;

    /* Jump to previous/next scene */
    if (key_pressed(KEY_LEFT) && current_scene_idx > 0)
    {
      current_scene_idx--;
      need_seek = 1;
    }
    else if (key_pressed(KEY_RIGHT) &&
             current_scene_idx < number_of_scenes - 1)
    {
      current_scene_idx++;
      need_seek = 1;
    }
    else if (current_scene->duration_ms > 0 &&
             elapsed >= current_scene->duration_ms)
    {
      /* Auto-advance */
      if (++current_scene_idx >= number_of_scenes)
      {
        if (loop)
        {
          current_scene_idx = 0;
          need_seek = 1;
        }
        else
          break;
      }
    }
    else
    {
      continue;
    }

    /* Scene changed */
    need_init = 1;
    current_scene = &timeline[current_scene_idx];
    if (need_seek)
    {
      audio_seek(current_scene->music_offset_ms);
      need_seek = 0;
    }
  }

  if (stats)
  {
    stats->total_frames = frames;
    stats->total_ms = timer_ms() - run_start;
  }
}

/*
 * Offline render: advance a virtual clock by exactly 1000/60 ms per
 * frame, render + capture each frame, render the matching chunk of
 * audio into the WAV sink via libxmp. No keyboard navigation; scene
 * selection via CLI still works. ESC aborts.
 */
static void run_timeline_offline(const TimelineEntry *timeline,
                                 const Asset *song, TimelineStats *stats,
                                 int number_of_scenes,
                                 unsigned char *backbuffer)
{
  /* Stereo short buffer sized for ~800 samples/frame, enough for any
   * AUDIO_RATE up to 48 kHz sampled at 60 fps. */
  static short frame_audio[800 * 2];
  unsigned int rate = audio_rate();
  unsigned long frames = 0;
  unsigned long sample_acc = 0;
  unsigned long run_start;
  int current_scene_idx = 0;
  int need_init = 1;
  const TimelineEntry *current_scene = &timeline[0];

  if (song)
    audio_load(*song, current_scene->music_offset_ms);

  run_start = timer_ms();

  while (!key_down(KEY_ESC))
  {
    unsigned long virtual_ms;
    unsigned long elapsed;
    int samples;

    virtual_ms = frames * 1000UL / 60UL;
    elapsed = virtual_ms - current_scene->music_offset_ms;

    {
      RenderContext ctx;
      ctx.backbuffer = backbuffer;
      ctx.ms = elapsed;
      ctx.timeline_ms = virtual_ms;
      ctx.frame = (unsigned int)MS_TO_FRAME(elapsed);
      ctx.timeline_frame = (unsigned int)MS_TO_FRAME(virtual_ms);

      if (need_init)
      {
        current_scene->scene->init(&ctx);
        need_init = 0;
      }
      current_scene->scene->render(&ctx);
    }

    capture_frame(backbuffer);

    /* Audio chunk for this frame. Accumulator splits `rate` evenly across
     * 60 frames (e.g. 22050/60 = 367 or 368 alternating) so the cumulative
     * sample count lands on rate × seconds exactly. */
    sample_acc += rate;
    samples = (int)(sample_acc / 60UL);
    sample_acc -= (unsigned long)samples * 60UL;
    if (samples > (int)(sizeof(frame_audio) / sizeof(frame_audio[0]) / 2))
      samples = (int)(sizeof(frame_audio) / sizeof(frame_audio[0]) / 2);
    audio_render_samples(frame_audio, samples);
    capture_audio(frame_audio, samples);

    frames++;

    /* Auto-advance when the virtual clock reaches this scene's end. */
    if (current_scene->duration_ms > 0 &&
        elapsed >= current_scene->duration_ms)
    {
      if (++current_scene_idx >= number_of_scenes)
        break;
      current_scene = &timeline[current_scene_idx];
      audio_seek(current_scene->music_offset_ms);
      need_init = 1;
    }
  }

  if (stats)
  {
    stats->total_frames = frames;
    stats->total_ms = timer_ms() - run_start;
  }
}

void run_timeline(const TimelineEntry *timeline, const Asset *song, int loop,
                  TimelineStats *stats)
{
  int number_of_scenes;
  unsigned char *backbuffer;

  number_of_scenes = prepare_timeline(timeline);
  if (number_of_scenes == 0)
    return;

  backbuffer =
      (unsigned char *)mem_alloc_offset(VGA_SIZE, MEM_OFFSET_BACKBUFFER);

  if (capture_enabled())
    run_timeline_offline(timeline, song, stats, number_of_scenes, backbuffer);
  else
    run_timeline_realtime(timeline, song, loop, stats, number_of_scenes,
                          backbuffer);

  shutdown_timeline(timeline, number_of_scenes);
  mem_free_aligned(backbuffer);
}
