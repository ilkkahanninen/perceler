#ifndef SCENE_H
#define SCENE_H

typedef struct {
  void (*setup)(void);
  void (*init)(void);
  void (*shutdown)(void);
  void (*render)(unsigned int draw_page, unsigned char frame);
} Scene;

typedef struct {
  const Scene *scene;
  unsigned long duration_ms;     /* 0 = run until ESC */
  unsigned long music_offset_ms; /* absolute position in the song */
} TimelineEntry;

/*
 * Run through a timeline of scenes.
 * Returns when the timeline ends or ESC is pressed.
 * Terminate the array with { NULL, 0 }.
 * If loop is non-zero, restart from the beginning after the last scene.
 */
void run_timeline(const TimelineEntry *timeline, int loop);

#endif
