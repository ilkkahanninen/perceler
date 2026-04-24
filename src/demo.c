#include "demo.h"

#include "scenes/model_viewer.h"
#include "scenes/plasma.h"
#include "scenes/polyhedra.h"
#include "scenes/tunnel.h"
#include "utils/timing.h"

#define BPM 162
#define SPEED 3
#define PATTERN_LEN 128

TimelineEntry demo_timeline[] = {
    {&model_viewer_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&model_viewer_flatshade_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&polyhedra_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&plasma_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&tunnel_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};

const Asset *demo_song(void)
{
  return &ASSET_J9_THGHT_XM;
}
