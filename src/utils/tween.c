/*
 * tween.c - Keyframe-based parameter tweening.
 *
 * Values are 8.8 fixed-point ints. The time-progress parameter t is
 * computed in Q16 (0..T_ONE) internally so a 10-second segment sampled
 * at 60fps advances t by ~109 per frame, smoothly within a 16-bit
 * fractional scale.
 *
 * Curve math and the final lerp use `long long` for the multiply
 * intermediates — several operations (t² at Q16, (v_b - v_a) * t_q16
 * for Q8.8 values near the edge of their ±128 range) exceed signed 32-bit.
 */

#include "tween.h"

#define T_Q 16
#define T_ONE 65536

static int apply_curve(TweenCurve curve, int t)
{
  switch (curve)
  {
  case TWEEN_STEP:
    return 0;
  case TWEEN_SMOOTH:
  {
    /* 3t² - 2t³  =  t² · (3 − 2t)  in Q16. */
    long long t2 = ((long long)t * t) >> T_Q;
    return (int)((t2 * (3 * (long long)T_ONE - 2 * (long long)t)) >> T_Q);
  }
  case TWEEN_EASE_IN:
    return (int)(((long long)t * t) >> T_Q);
  case TWEEN_EASE_OUT:
  {
    long long u = (long long)T_ONE - t;
    return (int)(T_ONE - ((u * u) >> T_Q));
  }
  case TWEEN_LINEAR:
  default:
    return t;
  }
}

int tween_at(const Tween *tween, unsigned long time_ms)
{
  const Keyframe *keys = tween->keys;
  int n = tween->num_keys;
  const Keyframe *a;
  const Keyframe *b;
  unsigned long span;
  int t_q16;
  int i;
  long long diff;

  if (n <= 0)
    return 0;
  if (time_ms <= keys[0].time_ms)
    return keys[0].value;
  if (time_ms >= keys[n - 1].time_ms)
    return keys[n - 1].value;

  /* Linear scan for the segment containing time_ms. Typical demo tweens
   * have only a handful of keys; binary search can come later. */
  for (i = 0; i < n - 1; i++)
    if (time_ms < keys[i + 1].time_ms)
      break;

  a = &keys[i];
  b = &keys[i + 1];
  span = b->time_ms - a->time_ms;
  if (span == 0)
    return a->value;

  /* t in Q16. 64-bit numerator survives long spans. */
  t_q16 = (int)(((unsigned long long)(time_ms - a->time_ms) * T_ONE) / span);
  t_q16 = apply_curve(a->out_curve, t_q16);

  /* Lerp: a.value + (b.value - a.value) * t_q16 / T_ONE. */
  diff = (long long)(b->value - a->value) * t_q16;
  return a->value + (int)(diff >> T_Q);
}

int spline_at(const Spline *spline, unsigned long time_ms)
{
  const SplineKey *keys;
  int n, i;
  int p0, p1, p2, p3;
  long long c1, c2, c3;
  long long t_q16, t2_q16, t3_q16;
  long long sum;
  unsigned long span;

  if (!spline)
    return 0;
  keys = spline->keys;
  n = spline->num_keys;
  if (n <= 0)
    return 0;
  if (n == 1)
    return keys[0].value;
  if (time_ms <= keys[0].time_ms)
    return keys[0].value;
  if (time_ms >= keys[n - 1].time_ms)
    return keys[n - 1].value;

  /* Linear scan for the segment containing time_ms — same shape as
   * tween_at(). */
  for (i = 0; i + 1 < n; i++)
    if (time_ms < keys[i + 1].time_ms)
      break;

  /* Catmull-Rom needs the two keys flanking the segment. Clamp at the
   * boundaries so the first and last segments use a doubled endpoint
   * for their missing neighbour — the curve flat-tangents past the
   * authored range, matching the clamp behaviour of the early-out
   * checks above. */
  p0 = (i > 0) ? keys[i - 1].value : keys[i].value;
  p1 = keys[i].value;
  p2 = keys[i + 1].value;
  p3 = (i + 2 < n) ? keys[i + 2].value : keys[i + 1].value;

  span = keys[i + 1].time_ms - keys[i].time_ms;
  if (span == 0)
    return p1;

  /* t in Q16 within this segment, then t² and t³ in the same scale. */
  t_q16 = ((unsigned long long)(time_ms - keys[i].time_ms) * T_ONE) / span;
  t2_q16 = (t_q16 * t_q16) >> T_Q;
  t3_q16 = (t2_q16 * t_q16) >> T_Q;

  /* Catmull-Rom: p(t) = 0.5 * (2p1 + (-p0+p2)t
   *                                + (2p0-5p1+4p2-p3)t²
   *                                + (-p0+3p1-3p2+p3)t³).
   * Coefficients evaluated as long long to absorb the Q16 multiply. */
  c1 = (long long)(-p0 + p2);
  c2 = (long long)(2 * p0 - 5 * p1 + 4 * p2 - p3);
  c3 = (long long)(-p0 + 3 * p1 - 3 * p2 + p3);

  sum = (long long)(2 * p1)
      + ((c1 * t_q16) >> T_Q)
      + ((c2 * t2_q16) >> T_Q)
      + ((c3 * t3_q16) >> T_Q);
  return (int)(sum >> 1);
}
