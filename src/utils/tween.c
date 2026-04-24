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
