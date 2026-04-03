#include "demo.h"

#include "scenes/plasma.h"
#include "scenes/tunnel.h"
#include "utils/xmtiming.h"

#define BPM 162
#define SPEED 3
#define PATTERN_LEN 128

TimelineEntry demo_timeline[] = {
    {&plasma_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&tunnel_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};
