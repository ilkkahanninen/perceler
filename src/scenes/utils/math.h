#ifndef MATH_H
#define MATH_H

/* Precomputed sine table: 256 entries, values 0-255.
 * sintab[i] = 128 + 127 * sin(i * pi / 128)
 */
extern const unsigned char sintab[256];

/*
 * 8.8 fixed-point arithmetic.
 *
 * Values represent signed numbers with 8 integer bits and 8 fractional bits.
 * All multiply/divide intermediates fit in 32 bits for values up to ~±127.
 *
 * Usage:
 *   int x = INT_TO_FP(3);           // 3.0
 *   int y = FP_MUL(x, sin8(angle)); // 3.0 * sin(angle)
 *   int px = FP_TO_INT(y);          // truncate to integer
 */

#define SWAP(a, b) \
  do               \
  {                \
    int t_ = (a);  \
    (a) = (b);     \
    (b) = t_;      \
  } while (0)

#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)
#define FP_HALF (1 << (FP_SHIFT - 1))
#define INT_TO_FP(x) ((x) << FP_SHIFT)
#define FP_TO_INT(x) ((x) >> FP_SHIFT)

/* Convert a float to 8.8 fixed-point with round-to-nearest. Suitable
 * for asset-compile-time / setup-time conversions; the argument is
 * evaluated twice, so pass a plain value or copy into a temporary. */
#define FLOAT_TO_FP(x) \
  ((int)((x) * 256.0f + ((x) >= 0.0f ? 0.5f : -0.5f)))

/* Fixed-point multiplication: (a * b) >> 8 */
#define FP_MUL(a, b) ((int)(((long)(a) * (long)(b)) >> FP_SHIFT))

/* Fixed-point division: (a << 8) / b */
#define FP_DIV(a, b) ((int)(((long)(a) << FP_SHIFT) / (b)))

/*
 * Signed 8.8 fixed-point sine and cosine.
 * Input: 0-255 maps to 0 - 2*pi.
 * Output: approximately -1.0 .. +1.0 in 8.8 fp.
 */
static inline int sin8(unsigned char a)
{
  return ((int)sintab[a] - 128) << 1;
}

static inline int cos8(unsigned char a)
{
  return sin8(a + 64);
}

/*
 * Fast LCG pseudo-random generators.
 *
 * Usage:
 *   unsigned int seed = 0x12345678;       // any non-zero starting state
 *   unsigned char n  = rand8(&seed);      // 0..255
 *   unsigned short s = rand16(&seed);     // 0..65535
 *   unsigned int   r = rand32(&seed);     // full 32-bit
 */

#define LCG_MUL 1103515245u
#define LCG_ADD 12345u

static inline unsigned int rand32(unsigned int *seed)
{
  *seed = *seed * LCG_MUL + LCG_ADD;
  return *seed;
}

static inline unsigned short rand16(unsigned int *seed)
{
  *seed = *seed * LCG_MUL + LCG_ADD;
  return (unsigned short)(*seed >> 16);
}

static inline unsigned char rand8(unsigned int *seed)
{
  *seed = *seed * LCG_MUL + LCG_ADD;
  return (unsigned char)(*seed >> 24);
}

#endif
