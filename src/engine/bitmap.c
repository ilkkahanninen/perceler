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

#include <stdio.h>
#include <stdlib.h>
#include "bitmap.h"
#include "modex.h"

/* ------------------------------------------------------------------ */
/* Little-endian helpers                                                */
/* ------------------------------------------------------------------ */
static unsigned short read_u16(FILE *f)
{
    unsigned char b[2];
    fread(b, 1, 2, f);
    return (unsigned short)(b[0] | ((unsigned short)b[1] << 8));
}

static unsigned long read_u32(FILE *f)
{
    unsigned char b[4];
    fread(b, 1, 4, f);
    return (unsigned long)b[0]
         | ((unsigned long)b[1] <<  8)
         | ((unsigned long)b[2] << 16)
         | ((unsigned long)b[3] << 24);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
Bitmap *bitmap_load(const char *path)
{
    FILE          *f;
    Bitmap        *bmp;
    unsigned long  pixel_offset;
    int            width, height, stride, y, i;
    unsigned short bit_count;
    unsigned long  compression;
    unsigned char  ctab[256][4]; /* BMP color table: B, G, R, reserved */

    f = fopen(path, "rb");
    if (!f) return NULL;

    /* --- File header (14 bytes) --- */
    if (fgetc(f) != 'B' || fgetc(f) != 'M') goto fail;
    read_u32(f);             /* file size   (ignored) */
    read_u32(f);             /* reserved             */
    pixel_offset = read_u32(f);

    /* --- Info header (40 bytes) --- */
    read_u32(f);             /* header size (ignored) */
    width       = (int)read_u32(f);
    height      = (int)read_u32(f);
    read_u16(f);             /* color planes          */
    bit_count   = read_u16(f);
    compression = read_u32(f);

    if (bit_count != 8 || compression != 0) goto fail;
    if (width <= 0 || height <= 0)          goto fail;

    read_u32(f); /* image size      */
    read_u32(f); /* X pixels/meter  */
    read_u32(f); /* Y pixels/meter  */
    read_u32(f); /* colors used     */
    read_u32(f); /* colors important */

    /* --- Color table (256 * 4 bytes = 1024 bytes) --- */
    fread(ctab, 4, 256, f);

    /* --- Allocate --- */
    bmp = (Bitmap *)malloc(sizeof(Bitmap));
    if (!bmp) goto fail;

    bmp->width  = width;
    bmp->height = height;
    bmp->pixels = (unsigned char *)malloc((unsigned)(width * height));
    if (!bmp->pixels) { free(bmp); goto fail; }

    /* Convert BGRA color table to 6-bit VGA RGB */
    for (i = 0; i < 256; i++) {
        bmp->palette[i][0] = ctab[i][2] >> 2; /* R */
        bmp->palette[i][1] = ctab[i][1] >> 2; /* G */
        bmp->palette[i][2] = ctab[i][0] >> 2; /* B */
    }

    /* --- Pixel data: bottom-to-top, stride rounded up to 4 bytes --- */
    stride = (width + 3) & ~3;
    fseek(f, (long)pixel_offset, SEEK_SET);

    for (y = height - 1; y >= 0; y--) {
        fread(bmp->pixels + y * width, 1, (unsigned)width, f);
        if (stride > width)
            fseek(f, (long)(stride - width), SEEK_CUR);
    }

    fclose(f);
    return bmp;

fail:
    fclose(f);
    return NULL;
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
