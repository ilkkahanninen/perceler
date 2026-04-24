/*
 * Perceler - Main entry point
 *
 * Optional command-line arguments: scene indices (0-based) to run.
 * When indices are given, only those scenes are loaded and the demo loops.
 * Example: demo.exe 0 2  — runs scenes 0 and 2 in a loop.
 */

#include "../demo.h"
#include "audio.h"
#include "capture.h"
#include "keyboard.h"
#include "scene.h"
#include "timer.h"
#include "vga.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
  TimelineEntry selected[17];
  TimelineStats stats;
  int have_selection;
  int num_scenes;
  const char *capture_prefix = getenv("PERCELER_CAPTURE");

  /* Capture-mode setup must precede the hardware-touching inits so the
   * capture files open cleanly from text mode and audio stays in offline
   * mode (no DMA hookup). */
  if (capture_prefix && capture_prefix[0])
  {
    audio_set_offline(1);
    capture_init(capture_prefix);
  }

  vga_init();
  keyboard_init();
  timer_init();
  audio_init();

  num_scenes = timeline_init(demo_timeline);

  have_selection = argc > 1 && timeline_select(argc, argv, demo_timeline,
                                               num_scenes, selected, 16);

  if (have_selection)
    run_timeline(selected, demo_song(), 1, &stats);
  else
    run_timeline(demo_timeline, demo_song(), 0, &stats);

  audio_shutdown();
  timer_shutdown();
  keyboard_shutdown();
  vga_exit();
  capture_shutdown();

  if (stats.total_ms > 0)
    printf("Average FPS: %lu\n", stats.total_frames * 1000 / stats.total_ms);

  return 0;
}
