/*
 * scene.c - Timeline-driven scene runner
 *
 * Supports jumping between scenes with left/right arrow keys.
 * Music position is synchronized when jumping.
 */

#include "scene.h"

#include "audio.h"
#include "keyboard.h"
#include "utils/mem.h"
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

void run_timeline(const TimelineEntry *timeline, const Asset *song, int loop,
                  TimelineStats *stats)
{
  unsigned long scene_start, elapsed, run_start;
  unsigned long frames = 0;
  int number_of_scenes, current_scene_idx, need_init, need_seek;
  int total_steps;
  const TimelineEntry *current_scene;
  unsigned char *backbuffer =
      (unsigned char *)mem_alloc_offset(VGA_SIZE, MEM_OFFSET_BACKBUFFER);

  for (number_of_scenes = 0; timeline[number_of_scenes].scene != 0;
       number_of_scenes++)
    ;
  if (number_of_scenes == 0)
    return;

  total_steps = number_of_scenes + 1;
  draw_progress(0, total_steps);

  if (song)
    audio_load(*song);
  draw_progress(1, total_steps);

  for (current_scene_idx = 0; current_scene_idx < number_of_scenes;
       current_scene_idx++)
  {
    timeline[current_scene_idx].scene->setup();
    draw_progress(current_scene_idx + 2, total_steps);
  }

  current_scene_idx = 0;
  need_init = 1;
  need_seek = 0;
  current_scene = &timeline[0];
  scene_start = timer_ms();
  audio_seek(current_scene->music_offset_ms);
  run_start = timer_ms();

  while (!key_down(KEY_ESC))
  {
    if (need_init)
    {
      current_scene->scene->init(backbuffer);
      need_init = 0;
    }

    audio_update();

    elapsed = timer_ms() - scene_start;
    current_scene->scene->render(
        backbuffer,
        (unsigned int)((elapsed * 61) >> 10),
        (unsigned int)(((current_scene->music_offset_ms + elapsed) * 61) >> 10));

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
          current_scene_idx = 0;
        else
          break;
      }
    }
    else
    {
      continue;
    }

    /* Scene changed — reset timing */
    need_init = 1;
    scene_start = timer_ms();
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

  for (current_scene_idx = 0; current_scene_idx < number_of_scenes;
       current_scene_idx++)
    timeline[current_scene_idx].scene->shutdown();

  mem_free_aligned(backbuffer);
}
