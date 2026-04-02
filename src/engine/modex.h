#ifndef MODEX_H
#define MODEX_H

#define MODEX_WIDTH 320
#define MODEX_HEIGHT 240

/* Page offsets in VGA memory (each page = 320*240/4 = 19200 bytes) */
#define MODEX_PAGE_SIZE 19200
#define MODEX_PAGE0 0
#define MODEX_PAGE1 MODEX_PAGE_SIZE

/* VGA memory as a near pointer (flat model, DOS/32A maps low 1MB linearly) */
#define MODEX_VGAMEM ((volatile unsigned char *)0xA0000)

void modex_init(void);
void modex_exit(void);
void modex_vsync(void);
void modex_setpage(unsigned int offset);
void modex_putpixel(int x, int y, unsigned char color, unsigned int page);
void modex_setplane(int plane);
void modex_clear(unsigned char color, unsigned int page);
void modex_setpalette(unsigned char index, unsigned char r, unsigned char g, unsigned char b);

#endif
