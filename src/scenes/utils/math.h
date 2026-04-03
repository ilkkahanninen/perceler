#ifndef MATH_H
#define MATH_H

/* Precomputed sine table: 256 entries, values 0-255.
 * sintab[i] = 128 + 127 * sin(i * pi / 128)
 */
extern const unsigned char sintab[256];

#endif
