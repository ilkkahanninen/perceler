#ifndef TWEEN_H
#define TWEEN_H

/*
 * Keyframe-based parameter tweening on a millisecond timeline.
 *
 * Values are stored as 8.8 fixed-point integers (the project-wide math
 * convention — see src/scenes/utils/math.h). `256 * x` represents the
 * real number `x`; valid range is roughly [-128.0, +128.0).
 *
 * Build a const array of Keyframes in ascending time order, wrap with
 * TWEEN(), and sample with tween_at() against audio_music_ms() or any
 * other ms clock.
 *
 * Each keyframe's `out_curve` controls interpolation across the segment
 * from that keyframe to the NEXT one. The curve on the last keyframe
 * is unused.
 *
 * Before keys[0].time_ms the value clamps to keys[0].value. After
 * keys[last].time_ms it clamps to keys[last].value.
 *
 * Usage:
 *   static const Keyframe cam_x_keys[] = {
 *     {    0,   0 * 256, TWEEN_SMOOTH },  // 0.0
 *     { 2000, 120 * 256, TWEEN_LINEAR },  // 120.0
 *     { 4000, 120 * 256, TWEEN_STEP   },  // hold
 *     { 6000, 120 * 256, TWEEN_SMOOTH },
 *     { 8000,   0 * 256, TWEEN_LINEAR },  // last: out_curve ignored
 *   };
 *   static const Tween cam_x = TWEEN(cam_x_keys);
 *
 *   // in render:
 *   int x_88 = tween_at(&cam_x, audio_music_ms());
 *   int x    = x_88 >> 8;  // to integer pixels
 */

typedef enum
{
  TWEEN_LINEAR,  /* straight ramp                              */
  TWEEN_STEP,    /* hold keys[i].value until keys[i+1].time_ms */
  TWEEN_SMOOTH,  /* smoothstep: 3t² - 2t³ (ease in and out)    */
  TWEEN_EASE_IN, /* t² (slow start, fast finish)               */
  TWEEN_EASE_OUT /* 1 - (1-t)² (fast start, slow finish)       */
} TweenCurve;

typedef struct
{
  unsigned long time_ms;
  int value; /* 8.8 fixed-point */
  TweenCurve out_curve;
} Keyframe;

typedef struct
{
  const Keyframe *keys;
  int num_keys;
} Tween;

#define TWEEN(keys_array) \
  { (keys_array), (int)(sizeof(keys_array) / sizeof((keys_array)[0])) }

/* Sample the tween at the given millisecond timestamp. Returns 8.8 fp. */
int tween_at(const Tween *tween, unsigned long time_ms);

/* Same as tween_at() but drops the 8-bit fractional part — useful when
 * the tween output feeds integer pixel coordinates. */
static inline int tween_at_int(const Tween *tween, unsigned long time_ms)
{
  return tween_at(tween, time_ms) >> 8;
}

#endif
