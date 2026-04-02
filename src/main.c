/*
 * DOS Demo - Main entry point
 */

#include "engine/modex.h"
#include "engine/keyboard.h"
#include "engine/audio.h"
#include "scenes/plasma.h"

int main(void)
{
    unsigned int draw_page;
    unsigned char frame = 0;

    modex_init();
    keyboard_init();
    audio_init();
    audio_load("music.xm");
    plasma_init();

    draw_page = MODEX_PAGE1;

    while (!key_pressed(KEY_ESC))
    {
        plasma_render(draw_page, frame);

        modex_setpage(draw_page);
        modex_vsync();
        audio_update();

        draw_page = (draw_page == MODEX_PAGE0) ? MODEX_PAGE1 : MODEX_PAGE0;
        frame++;
    }

    audio_shutdown();
    keyboard_shutdown();
    modex_exit();

    return 0;
}
