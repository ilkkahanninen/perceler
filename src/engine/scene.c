/*
 * scene.c - Timeline-driven scene runner
 *
 * Supports jumping between scenes with left/right arrow keys.
 * Music position is synchronized when jumping.
 */

#include "scene.h"

#include "audio.h"
#include "keyboard.h"
#include "timer.h"
#include "vga.h"

#include <stdlib.h>

int timeline_init(TimelineEntry *tl) {
  int n = 0;
  unsigned long offset = 0;
  while (tl[n].scene) {
    tl[n].music_offset_ms = offset;
    offset += tl[n].duration_ms;
    n++;
  }
  return n;
}

int timeline_select(int argc, char *argv[], const TimelineEntry *source,
                    int source_len, TimelineEntry *dest, int max) {
  int i, n = 0;
  for (i = 1; i < argc && n < max; i++) {
    int idx = atoi(argv[i]);
    if (idx >= 0 && idx < source_len)
      dest[n++] = source[idx];
  }
  dest[n].scene = 0;
  dest[n].duration_ms = 0;
  dest[n].music_offset_ms = 0;
  return n;
}

void run_timeline(const TimelineEntry *timeline, int loop,
                  TimelineStats *stats) {
  unsigned long scene_start, elapsed, run_start;
  unsigned long frames = 0;
  int number_of_scenes, current_scene, need_init, need_seek;

  for (number_of_scenes = 0; timeline[number_of_scenes].scene != 0;
       number_of_scenes++)
    ;
  if (number_of_scenes == 0)
    return;

  for (current_scene = 0; current_scene < number_of_scenes; current_scene++)
    timeline[current_scene].scene->setup();

  current_scene = 0;
  need_init = 1;
  need_seek = 0;
  scene_start = timer_ms();
  audio_seek(timeline[0].music_offset_ms);
  run_start = timer_ms();

  while (!key_pressed(KEY_ESC)) {
    if (need_init) {
      timeline[current_scene].scene->init();
      need_init = 0;
    }

    audio_update();
    vga_vsync();

    elapsed = timer_ms() - scene_start;
    timeline[current_scene].scene->render((unsigned char)(elapsed * 60 / 1000));

    frames++;

    /* Jump to previous/next scene */
    if (key_pressed(KEY_LEFT) && current_scene > 0) {
      current_scene--;
      need_seek = 1;
    } else if (key_pressed(KEY_RIGHT) && current_scene < number_of_scenes - 1) {
      current_scene++;
      need_seek = 1;
    } else if (timeline[current_scene].duration_ms > 0 &&
               elapsed >= timeline[current_scene].duration_ms) {
      /* Auto-advance */
      if (++current_scene >= number_of_scenes) {
        if (loop)
          current_scene = 0;
        else
          break;
      }
    } else {
      continue;
    }

    /* Scene changed — reset timing */
    need_init = 1;
    scene_start = timer_ms();
    if (need_seek) {
      audio_seek(timeline[current_scene].music_offset_ms);
      need_seek = 0;
    }
  }

  if (stats) {
    stats->total_frames = frames;
    stats->total_ms = timer_ms() - run_start;
  }

  for (current_scene = 0; current_scene < number_of_scenes; current_scene++)
    timeline[current_scene].scene->shutdown();
}
