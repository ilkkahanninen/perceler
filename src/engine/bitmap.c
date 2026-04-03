/*
 * bitmap.c - 8-bit indexed BMP loader
 *
 * Supports only uncompressed (BI_RGB) 8-bit BMPs.
 *
 * BMP pixel rows are stored bottom-to-top and padded to a 4-byte
 * stride; this loader flips and strips the padding so pixels[] is
 * a plain top-to-bottom, width*height byte array.
 *
 * BMP palette entries are BGRA with 8-bit channels; we shift right
 * by 2 to convert to the 6-bit range (0-63) expected by the VGA DAC.
 */

#include <stdlib.h>
#include <string.h>
#include "bitmap.h"
#include "modex.h"
#include "data.h"

/* ------------------------------------------------------------------ */
/* Little-endian helpers (memory buffer)                                */
/* ------------------------------------------------------------------ */
static unsigned short mem_u16(const unsigned char *p)
{
    return (unsigned short)(p[0] | ((unsigned short)p[1] << 8));
}

static unsigned long mem_u32(const unsigned char *p)
{
    return (unsigned long)p[0]
         | ((unsigned long)p[1] <<  8)
         | ((unsigned long)p[2] << 16)
         | ((unsigned long)p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* Core parser: works on a memory buffer                               */
/* ------------------------------------------------------------------ */
static Bitmap *bitmap_parse(const unsigned char *buf, unsigned long buf_len)
{
    Bitmap        *bmp;
    unsigned long  pixel_offset;
    int            width, height, stride, y, i;
    unsigned short bit_count;
    unsigned long  compression;
    const unsigned char *ctab;

    if (buf_len < 54) return 0;
    if (buf[0] != 'B' || buf[1] != 'M') return 0;

    /* File header (14 bytes) */
    pixel_offset = mem_u32(buf + 10);

    /* Info header (starts at offset 14) */
    width       = (int)mem_u32(buf + 18);
    height      = (int)mem_u32(buf + 22);
    bit_count   = mem_u16(buf + 28);
    compression = mem_u32(buf + 30);

    if (bit_count != 8 || compression != 0) return 0;
    if (width <= 0 || height <= 0) return 0;

    /* Color table starts at offset 54 (256 * 4 bytes = 1024 bytes) */
    if (buf_len < 54 + 1024) return 0;
    ctab = buf + 54;

    if (buf_len < pixel_offset) return 0;

    bmp = (Bitmap *)malloc(sizeof(Bitmap));
    if (!bmp) return 0;

    bmp->width  = width;
    bmp->height = height;
    bmp->pixels = (unsigned char *)malloc((unsigned)(width * height));
    if (!bmp->pixels) { free(bmp); return 0; }

    /* Convert BGRA color table to 6-bit VGA RGB */
    for (i = 0; i < 256; i++) {
        bmp->palette[i][0] = ctab[i * 4 + 2] >> 2; /* R */
        bmp->palette[i][1] = ctab[i * 4 + 1] >> 2; /* G */
        bmp->palette[i][2] = ctab[i * 4 + 0] >> 2; /* B */
    }

    /* Pixel data: bottom-to-top, stride rounded up to 4 bytes */
    stride = (width + 3) & ~3;
    for (y = height - 1; y >= 0; y--) {
        unsigned long src_row = pixel_offset + (unsigned long)(height - 1 - y) * stride;
        if (src_row + width > buf_len) { free(bmp->pixels); free(bmp); return 0; }
        memcpy(bmp->pixels + y * width, buf + src_row, (unsigned)width);
    }

    return bmp;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
Bitmap *bitmap_load(Asset asset)
{
    unsigned char *buf;
    Bitmap *bmp;

    buf = (unsigned char *)data_read(asset);
    if (!buf) return 0;

    bmp = bitmap_parse(buf, asset.length);
    free(buf);
    return bmp;
}

void bitmap_apply_palette(const Bitmap *bmp)
{
    int i;
    for (i = 0; i < 256; i++)
        modex_setpalette((unsigned char)i,
                         bmp->palette[i][0],
                         bmp->palette[i][1],
                         bmp->palette[i][2]);
}

void bitmap_blit(const Bitmap *bmp, int dx, int dy, unsigned int page)
{
    /*
     * Mode-X planar layout: plane = x & 3, VGA byte = page + y*80 + (x>>2).
     * We do 4 passes (one per plane) so modex_setplane() is called only 4
     * times total.  Color index 0 is skipped (transparent).
     */
    volatile unsigned char *vga = MODEX_VGAMEM;
    int plane, sx, sy;

    /* Clip source rect to both bitmap and screen bounds */
    int sx0 = (dx < 0) ? -dx : 0;
    int sy0 = (dy < 0) ? -dy : 0;
    int sx1 = bmp->width;
    int sy1 = bmp->height;

    if (dx + sx1 > MODEX_WIDTH)  sx1 = MODEX_WIDTH  - dx;
    if (dy + sy1 > MODEX_HEIGHT) sy1 = MODEX_HEIGHT - dy;

    if (sx0 >= sx1 || sy0 >= sy1) return; /* fully off-screen */

    for (plane = 0; plane < 4; plane++) {
        modex_setplane(plane);

        /* First source column that belongs to this plane */
        sx = sx0 + (((plane - (dx + sx0)) & 3 + 4) & 3);
        /* ^ bring sx0 up to the first x where (dx+sx) & 3 == plane */

        for (; sx < sx1; sx += 4) {
            const unsigned char *src = bmp->pixels + sy0 * bmp->width + sx;
            int screen_x = dx + sx;
            volatile unsigned char *dst =
                vga + page + (dy + sy0) * 80 + (screen_x >> 2);

            for (sy = sy0; sy < sy1; sy++, src += bmp->width, dst += 80) {
                if (*src != 0)
                    *dst = *src;
            }
        }
    }
}

void bitmap_free(Bitmap *bmp)
{
    if (!bmp) return;
    free(bmp->pixels);
    free(bmp);
}
