#ifndef DRAW_H
#define DRAW_H

/* Draw a line using Bresenham's algorithm with bounds clipping. */
void draw_line(unsigned char *buf, int x0, int y0, int x1, int y1,
               unsigned char color);

/* Fill a triangle with a solid color using scanline rasterization. */
void draw_triangle(unsigned char *buf, int x0, int y0, int x1, int y1,
                   int x2, int y2, unsigned char color);

#endif
