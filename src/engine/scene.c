/*
 * scene.c - Timeline-driven scene runner
 *
 * Supports jumping between scenes with left/right arrow keys.
 * Music position is synchronized when jumping.
 */

#include "scene.h"

#include "audio.h"
#include "keyboard.h"
#include "modex.h"
#include "timer.h"

void run_timeline(const TimelineEntry *timeline, int loop) {
  unsigned int draw_page = MODEX_PAGE1;
  unsigned long scene_start, elapsed;
  int number_of_scenes, current_scene, need_init;

  for (number_of_scenes = 0; timeline[number_of_scenes].scene != 0;
       number_of_scenes++)
    ;
  if (number_of_scenes == 0)
    return;

  for (current_scene = 0; current_scene < number_of_scenes; current_scene++)
    timeline[current_scene].scene->setup();

  current_scene = 0;
  need_init = 1;
  scene_start = timer_ms();
  audio_seek(timeline[0].music_offset_ms);

  while (!key_pressed(KEY_ESC)) {
    if (need_init) {
      timeline[current_scene].scene->init();
      need_init = 0;
    }

    elapsed = timer_ms() - scene_start;
    timeline[current_scene].scene->render(draw_page,
                                          (unsigned char)(elapsed * 60 / 1000));

    modex_setpage(draw_page);
    modex_vsync();
    audio_update();

    draw_page = (draw_page == MODEX_PAGE0) ? MODEX_PAGE1 : MODEX_PAGE0;

    /* Jump to previous/next scene */
    if (key_pressed(KEY_LEFT) && current_scene > 0)
      current_scene--;
    else if (key_pressed(KEY_RIGHT) && current_scene < number_of_scenes - 1)
      current_scene++;
    else if (timeline[current_scene].duration_ms > 0 &&
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
    audio_seek(timeline[current_scene].music_offset_ms);
  }

  for (current_scene = 0; current_scene < number_of_scenes; current_scene++)
    timeline[current_scene].scene->shutdown();
}
