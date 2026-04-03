/*
 * Perceler - Main entry point
 *
 * Optional command-line arguments: scene indices (0-based) to run.
 * When indices are given, only those scenes are loaded and the demo loops.
 * Example: demo.exe 0 2  — runs scenes 0 and 2 in a loop.
 */

#include "assets.h"
#include "engine/audio.h"
#include "engine/keyboard.h"
#include "engine/modex.h"
#include "engine/scene.h"
#include "engine/timer.h"
#include "scenes/plasma.h"
#include "scenes/tunnel.h"
#include "utils/xmtiming.h"

#include <stdlib.h>

#define BPM 162
#define SPEED 3
#define PATTERN_LEN 128

static TimelineEntry demo_timeline[] = {
    {&plasma_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&tunnel_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};

static int timeline_init(TimelineEntry *tl) {
  int n = 0;
  unsigned long offset = 0;
  while (tl[n].scene) {
    tl[n].music_offset_ms = offset;
    offset += tl[n].duration_ms;
    n++;
  }
  return n;
}

static int build_selection(int argc, char *argv[], const TimelineEntry *source,
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

int main(int argc, char *argv[]) {
  TimelineEntry selected[17];
  int have_selection;
  int num_scenes;

  modex_init();
  keyboard_init();
  timer_init();
  audio_init();
  audio_load(ASSET_J9_THGHT_XM);

  num_scenes = timeline_init(demo_timeline);

  have_selection = argc > 1 && build_selection(argc, argv, demo_timeline,
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
