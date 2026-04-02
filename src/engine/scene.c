/*
 * scene.c - Timeline-driven scene runner
 */

#include "scene.h"
#include "timer.h"
#include "modex.h"
#include "keyboard.h"
#include "audio.h"

void scene_run_timeline(const TimelineEntry *timeline)
{
    unsigned int draw_page = MODEX_PAGE1;
    unsigned char frame;
    unsigned long scene_start;
    const TimelineEntry *entry;

    /* Initialize all scenes */
    for (entry = timeline; entry->scene != 0; entry++)
        entry->scene->init();

    /* Run timeline */
    for (entry = timeline; entry->scene != 0 && !key_pressed(KEY_ESC); entry++) {
        frame = 0;
        scene_start = timer_ms();

        while (!key_pressed(KEY_ESC)) {
            entry->scene->render(draw_page, frame);

            modex_setpage(draw_page);
            modex_vsync();
            audio_update();

            draw_page = (draw_page == MODEX_PAGE0)
                      ? MODEX_PAGE1 : MODEX_PAGE0;
            frame++;

            if (entry->duration_ms > 0 &&
                (timer_ms() - scene_start) >= entry->duration_ms) {
                break;
            }
        }
    }

    /* Shutdown all scenes */
    for (entry = timeline; entry->scene != 0; entry++)
        entry->scene->shutdown();
}
