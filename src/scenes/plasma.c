/*
 * Plasma effect - classic sine-based plasma with color cycling
 *
 * Renders plane-by-plane (4 passes) to minimize VGA port I/O.
 */

#include <stdlib.h>
#include "plasma.h"
#include "../engine/modex.h"
#include "../engine/bitmap.h"
#include "../utils/sintab.h"
#include "assets.h"

static Bitmap *hello;

static void plasma_init(void)
{
    hello = bitmap_load(ASSET_HELLO_BMP);
}

static void plasma_shutdown(void)
{
    bitmap_free(hello);
    hello = NULL;
}

static void plasma_render(unsigned int draw_page, unsigned char frame)
{
    int plane, x, y;

    if (frame == 0)
        palette_apply(&hello->palette);

    for (plane = 0; plane < 4; plane++)
    {
        volatile unsigned char *dst;

        modex_setplane(plane);
        dst = MODEX_VGAMEM + draw_page;

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
}

const Scene plasma_scene = {
    plasma_init,
    plasma_shutdown,
    plasma_render};
