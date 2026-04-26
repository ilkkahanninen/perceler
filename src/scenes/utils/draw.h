#ifndef DRAW_H
#define DRAW_H

/* Draw a line from (x0,y0) to (x1,y1) on `buf` in `color`.
 * Clipped to 320×200. */
void draw_line(unsigned char *buf, int x0, int y0, int x1, int y1,
               unsigned char color);

/* Fill a triangle with vertices (x0,y0), (x1,y1), (x2,y2) on `buf`
 * in `color`. Clipped to 320×200. */
void draw_triangle(unsigned char *buf, int x0, int y0, int x1, int y1,
                   int x2, int y2, unsigned char color);

#endif
