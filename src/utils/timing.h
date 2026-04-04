#ifndef TIMING_H
#define TIMING_H

/* Calculate duration in milliseconds from XM timing parameters.
 * bpm:   tempo (beats per minute)
 * speed: ticks per row
 * rows:  number of rows
 */
#define XM_MS(bpm, speed, rows) \
  ((unsigned long)(rows) * (speed) * 2500UL / (bpm))

#endif
