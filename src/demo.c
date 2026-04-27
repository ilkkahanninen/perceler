#include "demo.h"

#include "scenes/model_flatshade.h"
#include "scenes/model_gouraud.h"
#include "scenes/model_wireframe.h"
#include "scenes/plasma.h"
#include "scenes/polyhedra.h"
#include "scenes/rope_knot.h"
#include "scenes/shaded_cube.h"
#include "scenes/spheremap.h"
#include "scenes/textured_cube.h"
#include "scenes/tunnel.h"
#include "utils/timing.h"

#define BPM 162
#define SPEED 3
#define PATTERN_LEN 128

TimelineEntry demo_timeline[] = {
    {&model_wireframe_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&model_flatshade_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&model_gouraud_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&textured_cube_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&shaded_cube_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&spheremap_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&rope_knot_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&polyhedra_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&plasma_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&tunnel_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {0, 0, 0}};

const Asset *demo_song(void)
{
  return &ASSET_J9_THGHT_XM;
}
