/*
 * DOS Demo - Mode-X Plasma Effect
 *
 * A classic sine-based plasma with color cycling, running in
 * Mode-X 320x240 with double buffering via page flipping.
 *
 * Renders plane-by-plane (4 passes) to minimize VGA port I/O.
 */

#include <math.h>
#include "modex.h"
#include "keyboard.h"
#include "audio.h"
#include "bitmap.h"

/* Precomputed sine table (256 entries, values 0-255) */
static unsigned char sintab[256];

static void init_sintab(void)
{
    int i;
    for (i = 0; i < 256; i++)
    {
        sintab[i] = (unsigned char)(128.0 + 127.0 * sin(i * 3.14159265 / 128.0));
    }
}

static void set_plasma_palette(void)
{
    int i;
    for (i = 0; i < 256; i++)
    {
        unsigned char r, g, b;
        r = (unsigned char)(31.0 * (1.0 + sin(i * 3.14159265 / 128.0)));
        g = (unsigned char)(31.0 * (1.0 + sin(i * 3.14159265 / 128.0 + 2.094)));
        b = (unsigned char)(31.0 * (1.0 + sin(i * 3.14159265 / 128.0 + 4.189)));
        modex_setpalette((unsigned char)i, r, g, b);
    }
}

/* VGA memory as a near pointer (flat model) */
#define VGAMEM ((volatile unsigned char *)0xA0000)

int main(void)
{
    unsigned int draw_page;
    unsigned char frame = 0;
    int plane, x, y;
    Bitmap *hello;

    init_sintab();
    modex_init();
    // set_plasma_palette();
    keyboard_init();
    audio_init();
    audio_load("music.xm");

    hello = bitmap_load("hello.bmp");
    bitmap_apply_palette(hello);

    draw_page = MODEX_PAGE1;

    while (!key_pressed(KEY_ESC))
    {
        /* Render plasma plane-by-plane: set plane once, write all its pixels */
        for (plane = 0; plane < 4; plane++)
        {
            volatile unsigned char *dst;

            modex_setplane(plane);
            dst = VGAMEM + draw_page;

            for (y = 0; y < MODEX_HEIGHT; y++)
            {
                for (x = plane; x < MODEX_WIDTH; x += 4)
                {
                    unsigned char col1, col2;
                    col1 = (sintab[(x + frame) & 0xFF] + sintab[(y + frame) & 0xFF]) >> 4;
                    col2 = (sintab[((x + y) / 2 + frame * 2) & 0xFF] + sintab[((x - y + 256) / 2 + frame * 3) & 0xFF]) & 0xF0;
                    dst[y * 80 + (x >> 2)] = col1 | col2;
                }
            }
        }

        if (hello)
            bitmap_blit(hello, (MODEX_WIDTH - hello->width) / 2,
                        (MODEX_HEIGHT - hello->height) / 2, draw_page);

        modex_setpage(draw_page);
        modex_vsync();
        audio_update();

        draw_page = (draw_page == MODEX_PAGE0) ? MODEX_PAGE1 : MODEX_PAGE0;
        frame++;
    }

    bitmap_free(hello);
    audio_shutdown();
    keyboard_shutdown();
    modex_exit();

    return 0;
}
