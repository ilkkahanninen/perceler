#ifndef SCENE_H
#define SCENE_H

#include "../assets.h"

/*
 * Per-frame context passed to a Scene's init() and render() callbacks.
 * Scene-relative fields drive animation; timeline-relative fields drive
 * music synchronisation.
 */
typedef struct
{
  unsigned char *backbuffer;
  unsigned int frame;          /* frames since this scene started */
  unsigned long ms;            /* ms since this scene started */
  unsigned int timeline_frame; /* frames since the timeline started */
  unsigned long timeline_ms;   /* ms since the timeline started */
} RenderContext;

/*
 * A scene's lifecycle:
 *   setup()    : called once at program start; load assets, allocate
 *                buffers
 *   init(ctx)  : called every time the scene becomes active in the
 *                timeline (e.g. after a jump back); reset palette etc.
 *   render(ctx): called once per frame while the scene is active
 *   shutdown() : called once at program end; release everything setup
 *                allocated
 */
typedef struct
{
  void (*setup)(void);
  void (*init)(const RenderContext *ctx);
  void (*shutdown)(void);
  void (*render)(const RenderContext *ctx);
} Scene;

typedef struct
{
  const Scene *scene;
  unsigned long duration_ms;     /* 0 = run until ESC */
  unsigned long music_offset_ms; /* absolute position in the song */
} TimelineEntry;

/* Compute music_offset_ms for each entry from cumulative durations.
 * Returns the number of scenes (excluding the sentinel). */
int timeline_init(TimelineEntry *tl);

/* Build a filtered timeline from command-line scene indices selected
 * out of `source`. Returns the number of entries written to `dest`. */
int timeline_select(int argc, char *argv[], const TimelineEntry *source,
                    int source_len, TimelineEntry *dest, int max);

typedef struct
{
  unsigned long total_frames;
  unsigned long total_ms;
} TimelineStats;

/* Run through `timeline` (terminated by a `{ NULL, 0, 0 }` sentinel),
 * playing `song` if non-NULL. Returns when the timeline ends or ESC is
 * pressed. If `loop` is non-zero the timeline restarts from the
 * beginning after the last scene. `stats` may be NULL. */
void run_timeline(const TimelineEntry *timeline, const Asset *song, int loop,
                  TimelineStats *stats);

#endif
