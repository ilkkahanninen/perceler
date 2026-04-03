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

static unsigned long music_offset(const TimelineEntry *timeline, int idx)
{
    unsigned long ms = 0;
    int i;
    for (i = 0; i < idx; i++)
        ms += timeline[i].duration_ms;
    return ms;
}

void scene_run_timeline(const TimelineEntry *timeline)
{
    unsigned int draw_page = MODEX_PAGE1;
    unsigned long scene_start, elapsed;
    int count, idx, need_init;

    for (count = 0; timeline[count].scene != 0; count++)
        ;
    if (count == 0) return;

    for (idx = 0; idx < count; idx++)
        timeline[idx].scene->setup();

    idx = 0;
    need_init = 1;
    scene_start = timer_ms();
    audio_seek(0);

    while (!key_pressed(KEY_ESC)) {
        if (need_init) {
            timeline[idx].scene->init();
            need_init = 0;
        }

        elapsed = timer_ms() - scene_start;
        timeline[idx].scene->render(draw_page,
            (unsigned char)(elapsed * 60 / 1000));

        modex_setpage(draw_page);
        modex_vsync();
        audio_update();

        draw_page = (draw_page == MODEX_PAGE0)
                  ? MODEX_PAGE1 : MODEX_PAGE0;

        /* Jump to previous/next scene */
        if (key_pressed(KEY_LEFT) && idx > 0)
            idx--;
        else if (key_pressed(KEY_RIGHT) && idx < count - 1)
            idx++;
        else if (timeline[idx].duration_ms > 0 &&
                 elapsed >= timeline[idx].duration_ms) {
            /* Auto-advance */
            if (++idx >= count)
                break;
        } else {
            continue;
        }

        /* Scene changed — reset timing */
        need_init = 1;
        scene_start = timer_ms();
        audio_seek(music_offset(timeline, idx));
    }

    for (idx = 0; idx < count; idx++)
        timeline[idx].scene->shutdown();
}
