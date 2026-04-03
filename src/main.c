/*
 * Perceler - Main entry point
 */

#include "assets.h"
#include "engine/audio.h"
#include "engine/keyboard.h"
#include "engine/modex.h"
#include "engine/scene.h"
#include "engine/timer.h"
#include "scenes/plasma.h"
#include "scenes/tunnel.h"

static const TimelineEntry demo_timeline[] = {
    {&plasma_scene, 10000}, {&tunnel_scene, 10000}, {0, 0}};

int main(void) {
  modex_init();
  keyboard_init();
  timer_init();
  audio_init();
  audio_load(ASSET_MUSIC_XM);

  run_timeline(demo_timeline);

  audio_shutdown();
  timer_shutdown();
  keyboard_shutdown();
  modex_exit();

  return 0;
}
