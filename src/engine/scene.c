/*
 * scene.c - Timeline-driven scene runner
 *
 * Supports jumping between scenes with left/right arrow keys.
 * Music position is synchronized when jumping.
 */

#include "scene.h"
#include "timer.h"
#include "modex.h"
#include "keyboard.h"
#include "audio.h"

void scene_run_timeline(const TimelineEntry *timeline)
{
    unsigned int draw_page = MODEX_PAGE1;
    unsigned long scene_start, elapsed;
    int count, idx;

    /* Count entries */
    for (count = 0; timeline[count].scene != 0; count++)
        ;
    if (count == 0) return;

    /* Initialize all scenes */
    for (idx = 0; idx < count; idx++)
        timeline[idx].scene->init();

    /* Compute cumulative music offset for each scene and seek to start */
    idx = 0;
    audio_seek(0);

    while (!key_pressed(KEY_ESC)) {
        scene_start = timer_ms();

        while (!key_pressed(KEY_ESC)) {
            int jumped = 0;

            elapsed = timer_ms() - scene_start;
            timeline[idx].scene->render(draw_page,
                (unsigned char)(elapsed * 60 / 1000));

            modex_setpage(draw_page);
            modex_vsync();
            audio_update();

            draw_page = (draw_page == MODEX_PAGE0)
                      ? MODEX_PAGE1 : MODEX_PAGE0;

            /* Jump to previous scene */
            if (key_pressed(KEY_LEFT) && idx > 0) {
                idx--;
                jumped = 1;
            }

            /* Jump to next scene */
            if (!jumped && key_pressed(KEY_RIGHT) && idx < count - 1) {
                idx++;
                jumped = 1;
            }

            if (jumped) {
                unsigned long music_ms = 0;
                int i;
                for (i = 0; i < idx; i++)
                    music_ms += timeline[i].duration_ms;
                audio_seek(music_ms);
                break;
            }

            /* Auto-advance when duration expires */
            if (timeline[idx].duration_ms > 0 &&
                elapsed >= timeline[idx].duration_ms) {
                idx++;
                if (idx >= count)
                    goto done;
                break;
            }
        }
    }

done:
    /* Shutdown all scenes */
    for (idx = 0; idx < count; idx++)
        timeline[idx].scene->shutdown();
}
