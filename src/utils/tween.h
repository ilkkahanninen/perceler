#ifndef TWEEN_H
#define TWEEN_H

/*
 * Keyframe-based parameter tweening on a millisecond timeline.
 *
 * Values are Q8.8 fixed-point integers; `256 * x` represents the real
 * number `x`; valid range is roughly [-128.0, +128.0).
 *
 * Build a const array of Keyframes in ascending time order, wrap with
 * TWEEN(), and sample with tween_at() against any ms timestamp.
 *
 * Each keyframe's `out_curve` controls interpolation across the
 * segment from that keyframe to the NEXT one; the curve on the last
 * keyframe is unused. Before keys[0].time_ms the value clamps to
 * keys[0].value; after keys[last].time_ms it clamps to keys[last].value.
 *
 * Example:
 *   static const Keyframe cam_x_keys[] = {
 *     {    0,   0 * 256, TWEEN_SMOOTH },  // 0.0
 *     { 2000, 120 * 256, TWEEN_LINEAR },  // 120.0
 *     { 4000, 120 * 256, TWEEN_STEP   },  // hold
 *     { 6000, 120 * 256, TWEEN_SMOOTH },
 *     { 8000,   0 * 256, TWEEN_LINEAR },  // last: out_curve ignored
 *   };
 *   static const Tween cam_x = TWEEN(cam_x_keys);
 *   int x = tween_at_int(&cam_x, ctx->ms);
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

/*
 * Catmull-Rom spline through a list of control points.
 *
 * Where Tween interpolates each segment in isolation with a chosen
 * curve, Spline reads the two neighbouring keys as well so the
 * curve is C1-continuous (no slope discontinuities) across all
 * keys. It passes through every key exactly. Endpoint tangents are
 * clamped so the curve flat-lines just past the first and last
 * key, leaving the value stable outside the authored range.
 *
 * Values are Q8.8 fixed-point; sampling cost is constant-time per
 * segment plus a linear scan to find the segment.
 *
 * Use Spline when you want smooth motion across more than two keys
 * — camera dollies, parameter sweeps, cinematic moves. For
 * per-segment easing (linear, smooth, ease-in/out, step) use Tween.
 *
 * Example: a cinematic camera-z sweep across six seconds.
 *
 *   static const SplineKey cam_z_keys[] = {
 *     {    0, INT_TO_FP(8) },   // start far back
 *     { 2000, INT_TO_FP(4) },   // dolly in
 *     { 4000, INT_TO_FP(3) },   // closest pass
 *     { 6000, INT_TO_FP(8) },   // pull back out
 *   };
 *   static const Spline cam_z = SPLINE(cam_z_keys);
 *
 *   // render():
 *   int z = spline_at(&cam_z, ctx->ms);
 */

typedef struct
{
  unsigned long time_ms;
  int value; /* 8.8 fixed-point */
} SplineKey;

typedef struct
{
  const SplineKey *keys;
  int num_keys;
} Spline;

#define SPLINE(keys_array) \
  { (keys_array), (int)(sizeof(keys_array) / sizeof((keys_array)[0])) }

/* Sample the spline at the given millisecond timestamp. Returns Q8.8.
 * For a spline of fewer than two keys, returns the single value (or
 * 0 for an empty spline). */
int spline_at(const Spline *spline, unsigned long time_ms);

/* Same as spline_at() but drops the 8-bit fractional part. */
static inline int spline_at_int(const Spline *spline, unsigned long time_ms)
{
  return spline_at(spline, time_ms) >> 8;
}

#endif
