# Music sync

Four ways to drive scenes from time and music, in increasing order of
authoring effort:

| Source             | Granularity            | Authored where                    | Read where           |
| ------------------ | ---------------------- | --------------------------------- | -------------------- |
| Frame / time       | Per frame (60 Hz)      | nowhere — comes from `ctx`        | inline arithmetic    |
| Keyframed tween    | Per ms                 | C array of `Keyframe`             | `tween_at()`         |
| Sample triggers    | Discrete note events   | `tools/xm_sample_frames.py`       | `sample_triggered()` |
| FFT band energy    | Per frame, 0..255      | `tools/wav2fft.py`                | `fft_at()`           |

Plus one timeline-level macro (`XM_MS`) for setting scene durations from
tracker math.

---

## Step 1 — pick your sync source

| You want…                                                   | Use                        |
| ----------------------------------------------------------- | -------------------------- |
| Smooth continuous motion (rotation, scrolling plasma)       | `ctx->frame` or `ctx->ms`  |
| A parameter that takes scripted values at specific times    | Keyframed tween on `->ms`  |
| Flash / spawn / advance state on every kick / snare / lead  | `SampleTrack`              |
| Continuous response to a frequency band (kick punch, etc.)  | FFT track + `fft_at()`     |
| Scene durations measured in tracker bars / patterns         | `XM_MS(bpm, speed, rows)`  |

These compose freely — a scene typically uses two or three of them
together (e.g. `frame` for rotation + tween for camera + FFT for palette
flash, as seen in [tunnel.c](../src/scenes/tunnel.c)).

---

## Step 2 — scene-relative vs timeline-relative time

`RenderContext` ([scene.h](../src/engine/scene.h)) exposes two clocks:

| Field             | Resets when…              | Use for                    |
| ----------------- | ------------------------- | -------------------------- |
| `frame` / `ms`    | This scene becomes active | Animation that should
                                                  restart with the scene  |
| `timeline_frame` / `timeline_ms` | Timeline starts | Music sync — values live
                                                  in song coordinates     |

**Rule of thumb**: anything authored against the song (sample trigger
frames, FFT tracks, beat-locked palette flashes) reads
`timeline_*`. Anything internal to the scene's own choreography reads
`frame` / `ms`.

If you `Left-arrow` back to a previous scene, scene-relative time
restarts from 0; timeline-relative time keeps going forward (or jumps to
match the song position). Sample triggers / FFT will stay in sync with
what's actually playing.

---

## Step 3 — wire up the source

### Frame counter

The most direct: read the integer frame number, derive whatever you need
inline.

```c
unsigned char ay = (unsigned char)ctx->frame;       /* 0..255 wraps */
unsigned char ax = (unsigned char)(ctx->frame >> 1);/* slower wrap   */
```

The 8-bit truncation is the standard idiom because [math.h](../src/scenes/utils/math.h)'s
`sin8` / `cos8` take an `unsigned char`. Shifting right slows it down by
factors of two; for non-power-of-two rates, multiply first then mask.

For frame → ms inside the scene, use `FRAME_MS(frame)` from
[timing.h](../src/utils/timing.h).

### Keyframed tween

For parameters that change scripted values at specific times — sliding
text, camera dollies, fade points. See [tween.h](../src/utils/tween.h) for the curve
options.

```c
static const Keyframe title_x_keys[] = {
    {    0, INT_TO_FP(-80),  TWEEN_EASE_OUT}, /* off-screen left */
    { 1200, INT_TO_FP(  6),  TWEEN_STEP    }, /* slide in, hold */
    { 5000, INT_TO_FP(  6),  TWEEN_EASE_IN }, /* still held, accel out */
    { 6200, INT_TO_FP(330),  TWEEN_LINEAR  }, /* off-screen right */
};
static const Tween title_x = TWEEN(title_x_keys);

font_draw(&font_default, backbuffer,
          tween_at_int(&title_x, ctx->ms), 4, 255, "plasma.c");
```

Times are scene-relative ms by convention (write keys against
`ctx->ms`). The `out_curve` on each key controls interpolation up to the
*next* key. Before the first key the value clamps; after the last it
clamps. Use `tween_at()` for Q8.8 output, `tween_at_int()` for integer.

#### Smoother motion across many keys: `Spline`

`Tween` interpolates each segment in isolation, so the curve is C1 only
when both adjacent keys agree on the curve type. For motion that
should stay smooth across more than two keys — camera dollies, multi-
stop parameter sweeps, anything cinematic — use `Spline` from the same
header. It runs Catmull-Rom through the control points: passes through
every key, C1-continuous everywhere, no per-segment curve enum needed.

```c
static const SplineKey cam_z_keys[] = {
    {    0, INT_TO_FP(8) },   /* start far back */
    { 2000, INT_TO_FP(4) },   /* dolly in       */
    { 4000, INT_TO_FP(3) },   /* closest pass   */
    { 6000, INT_TO_FP(8) },   /* pull back out  */
};
static const Spline cam_z = SPLINE(cam_z_keys);

/* render(): */
int z = spline_at(&cam_z, ctx->ms);
```

Endpoint tangents are clamped (the curve flat-lines just past the
first and last key) so values stay stable outside the authored
range. The values are Q8.8 throughout — pair with
[3d-graphics.md](3d-graphics.md#camera-tuning) when driving
`Camera3D` parameters.

### Sample triggers

For discrete note events from the XM song — kick hits, lead-in stabs,
etc. Two-step process:

1. **Author**: list the instruments first, then extract their frames.

   ```sh
   python3 tools/xm_sample_frames.py assets/song.xm --list
   python3 tools/xm_sample_frames.py assets/song.xm src/scenes/sync.h 8 16
   ```

   Generates `sample_8_frames[]` and `sample_16_frames[]` arrays of
   absolute timeline frame numbers, plus matching `_COUNT` defines.

2. **Read** with `SampleTrack` from [timing.h](../src/utils/timing.h):

   ```c
   #include "sync.h"

   static SampleTrack kick = SAMPLE_TRACK(sample_8_frames,
                                          SAMPLE_8_FRAMES_COUNT);
   /* ... in render(): */
   if (sample_triggered(&kick, ctx->timeline_frame))
       flash_intensity = 255;
   else
       flash_intensity = (flash_intensity * 7) >> 3; /* exp decay */
   ```

`sample_triggered()` returns 1 *exactly once* when `timeline_frame`
crosses each entry. The `SampleTrack` carries an internal cursor —
declare it `static` (or in `scene` state), not `const`.

### FFT band energy

For continuous response to a specific frequency band — the kick's
fundamental for screen-shake, the snare for palette flashes, the bass
for scrolling speed. Two-step:

1. **Author**: render the song to WAV, then to a per-band track. Lower
   bands react to drums and bass; higher bands react to leads and hats.

   ```sh
   ./tools/xm2wav.sh assets/song.xm build/song.wav
   python3 tools/wav2fft.py build/song.wav assets/song_kick.fft \
           --low 60 --high 200
   ```

   Output is a header-less stream of 60 Hz bytes (one per frame),
   normalised to 0..255 across the whole track. File length = frame
   count.

2. **Read** with `fft_load` / `fft_at` from [fft.h](../src/scenes/utils/fft.h):

   ```c
   static FFTTrack *kick;

   /* setup() */
   kick = fft_load(ASSET_SONG_KICK_FFT);

   /* render(): */
   int e = fft_at(kick, ctx->timeline_frame);  /* 0..255 */
   palette_fade(&pal_current, &pal_current, 63, 63, 63, e);
   ```

`fft_at` returns 0 outside the track range — safe to call on every
frame. Always free with `fft_free` in `shutdown()`.

Reference: [tunnel.c](../src/scenes/tunnel.c) flashes its palette toward white using
the snare-band energy.

#### From band energy to discrete events: `OnsetDetector`

Continuous energy is great for things that should *modulate* with the
music (palette fades, screen shake, scroll speed). For things that
should fire *once per audible hit* (a particle burst, a flash, a
state change), drive an `OnsetDetector` from the same FFT track:

```c
static FFTTrack *snare;
static OnsetDetector snare_onset;

/* setup() */
snare = fft_load(ASSET_SNARE_FFT);

/* init() */
onset_init(&snare_onset, 100 /* floor */, 8 /* cooldown frames */);

/* render() */
int e = fft_at(snare, ctx->timeline_frame);
if (onset_step(&snare_onset, e))
    /* fire — burst particles, set a flash flag, etc. */
```

`onset_step` returns 1 once per peak in the energy stream that clears
`floor`, suppressing further triggers for `cooldown_frames`. Tune the
two values to the band:

- **Floor too low** → noise floor and decay-tail bumps trigger
  spurious fires. Raise it.
- **Cooldown too short** → secondary bumps within the same hit's
  envelope re-fire. Raise it.
- **Cooldown too long** → fast back-to-back hits (16ths, drum rolls)
  get swallowed. Lower it.

Reference: [particles.c](../src/scenes/particles.c) uses `OnsetDetector` on
the snare-band track to fire a particle burst per hit. One-frame
latency: the trigger fires on the falling edge after a peak, so it's
delayed by one frame from the peak itself.

---

## Step 4 — derive scene durations from tracker timing

In `src/demo.c` each timeline entry has a duration in ms. Hard-coded ms
values rot the moment you change the song's tempo or layout. `XM_MS`
from [timing.h](../src/utils/timing.h) computes ms from tracker rows:

```c
#define BPM 162
#define SPEED 3
#define PATTERN_LEN 128

TimelineEntry demo_timeline[] = {
    {&intro_scene,    XM_MS(BPM, SPEED, PATTERN_LEN * 4)}, /* 4 patterns */
    {&drop_scene,     XM_MS(BPM, SPEED, PATTERN_LEN * 2)}, /* 2 patterns */
    {&outro_scene,    XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};
```

`XM_MS(bpm, speed, rows) = rows * speed * 2500 / bpm`. This is the same
formula the XM player uses internally, so scene boundaries land on row
boundaries to within rounding. Express durations as multiples of
`PATTERN_LEN` (or fractions of a bar) so the structure is readable.

If the song uses runtime tempo changes (`Fxx` effects with `xx >= 0x20`),
the simple `XM_MS` constant won't track them — author durations against
each section's local BPM and split the timeline at tempo changes.

---

## Common pitfalls

- **Reading `frame` for music sync.** Animation feels right for the
  *current* scene but desynchronises after a scene transition (or after
  scrubbing back). Use `timeline_frame` for anything that names a song
  event.
- **Sample triggers without `static`.** A `SampleTrack` carries a
  mutable cursor; declaring it `const` or making it a local variable
  resets the cursor every frame and you get either no triggers or all
  past triggers re-firing.
- **`SampleTrack` over scene boundaries.** The cursor advances forward
  only — once `index` has passed all the timeline frames, no further
  triggers fire even if the song wraps. For looping scenes / songs,
  re-initialise the track in `init()`.
- **FFT track at the wrong sample rate.** `wav2fft.py` defaults to
  60 fps; if you change the engine frame rate, regenerate every track.
  `fft_at` indexes by frame number, not by ms.
- **Hard-coded ms in the timeline.** A 4 BPM tempo bump invalidates
  every duration. Use `XM_MS`.
- **Authoring tweens against `timeline_ms`.** Tween times are usually
  scene-relative (so a tween restarts with the scene). If you really
  want a song-locked tween, pass `ctx->timeline_ms` and adjust the key
  times — but at that point a sample-trigger or FFT track is usually
  cleaner.
- **Calling `fft_load` per frame.** It allocates and copies the whole
  track. Load in `setup()`, free in `shutdown()`.

---

## Reference scenes

| Scene                                              | Demonstrates                                       |
| -------------------------------------------------- | -------------------------------------------------- |
| [plasma.c](../src/scenes/plasma.c)                 | `frame` for animation, `Tween` for title slide     |
| [tunnel.c](../src/scenes/tunnel.c)                 | `frame` for animation, FFT track for palette flash |
| [particles.c](../src/scenes/particles.c)                 | FFT energy as a particle-burst trigger             |
| Any 3D scene                                       | `frame` truncated to 8 bits as a rotation angle    |

Particle bursts are a natural pairing for sample triggers and FFT
energy — see [particles.md](particles.md) for the spawn-from-trigger
patterns.
