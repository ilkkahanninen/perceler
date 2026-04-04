#ifndef SCENE_H
#define SCENE_H

typedef struct {
  void (*setup)(void);
  void (*init)(unsigned char *backbuffer);
  void (*shutdown)(void);
  void (*render)(unsigned char *backbuffer, unsigned int frame);
} Scene;

typedef struct {
  const Scene *scene;
  unsigned long duration_ms;     /* 0 = run until ESC */
  unsigned long music_offset_ms; /* absolute position in the song */
} TimelineEntry;

/*
 * Compute music_offset_ms for each entry from cumulative durations.
 * Returns the number of scenes (excluding the sentinel).
 */
int timeline_init(TimelineEntry *tl);

/*
 * Build a filtered timeline from command-line scene indices.
 * Returns the number of selected scenes.
 */
int timeline_select(int argc, char *argv[], const TimelineEntry *source,
                    int source_len, TimelineEntry *dest, int max);

/*
 * Run through a timeline of scenes.
 * Returns when the timeline ends or ESC is pressed.
 * Terminate the array with { NULL, 0 }.
 * If loop is non-zero, restart from the beginning after the last scene.
 */
typedef struct {
  unsigned long total_frames;
  unsigned long total_ms;
} TimelineStats;

void run_timeline(const TimelineEntry *timeline, int loop, TimelineStats *stats);

#endif
