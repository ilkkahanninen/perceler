/*
 * Perceler - Main entry point
 *
 * Optional command-line arguments: scene indices (0-based) to run.
 * When indices are given, only those scenes are loaded and the demo loops.
 * Example: demo.exe 0 2  — runs scenes 0 and 2 in a loop.
 */

#include "demo.h"
#include "engine/audio.h"
#include "engine/keyboard.h"
#include "engine/modex.h"
#include "engine/scene.h"
#include "engine/timer.h"

int main(int argc, char *argv[]) {
  TimelineEntry selected[17];
  int have_selection;
  int num_scenes;

  modex_init();
  keyboard_init();
  timer_init();
  audio_init();
  audio_load(DEMO_SONG);

  num_scenes = timeline_init(demo_timeline);

  have_selection = argc > 1 && timeline_select(argc, argv, demo_timeline,
                                               num_scenes, selected, 16);

  if (have_selection)
    run_timeline(selected, 1);
  else
    run_timeline(demo_timeline, 0);

  audio_shutdown();
  timer_shutdown();
  keyboard_shutdown();
  modex_exit();

  return 0;
}
