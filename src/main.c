/*
 * DOS Demo - Main entry point
 */

#include "engine/modex.h"
#include "engine/keyboard.h"
#include "engine/audio.h"
#include "engine/timer.h"
#include "engine/scene.h"
#include "scenes/plasma.h"

static const TimelineEntry demo_timeline[] = {
    { &plasma_scene, 10000 },
    { 0, 0 }
};

int main(void)
{
    modex_init();
    keyboard_init();
    timer_init();
    audio_init();
    audio_load("music.xm");

    scene_run_timeline(demo_timeline);

    audio_shutdown();
    timer_shutdown();
    keyboard_shutdown();
    modex_exit();

    return 0;
}
