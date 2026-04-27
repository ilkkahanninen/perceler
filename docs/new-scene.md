# Setting up a new scene

A scene is a struct of four function pointers (the lifecycle) plus
whatever static state the scene needs. The engine calls the lifecycle
functions at well-defined moments and hands `render()` a `RenderContext`
each frame; everything else is up to the scene.

```
boot ─► setup() ──► init() ──► render() ──► render() ──► … ─┐
                      ▲                                     │
                      └──────── (re-enter via timeline) ────┘

quit ◄── shutdown() ◄── (last render of the run)
```

---

## Step 1 — scaffold the files

```sh
./tools/new_scene.sh starfield
```

Generates `src/scenes/starfield.{c,h}` wired up with empty lifecycle
stubs and a render that clears to black. The Makefile picks up new
files automatically — no changes needed to the build.

The script prints the line you need to add to the timeline; see
[Step 5](#step-5--wire-it-into-the-timeline).

---

## Step 2 — understand the lifecycle

Each scene exports a `const Scene` ([scene.h](../src/engine/scene.h#L30-L36)) holding
four function pointers:

| Function      | Called when                              | Do here                                   |
| ------------- | ---------------------------------------- | ----------------------------------------- |
| `setup()`     | Once at program start, before any render | Load assets, `malloc` buffers, build LUTs |
| `init(ctx)`   | Every time the scene becomes active      | Apply palette, reset mutable state        |
| `render(ctx)` | Once per frame while active (60 Hz)      | Draw to `ctx->backbuffer`, then blit      |
| `shutdown()`  | Once at program end                      | `free` everything `setup()` allocated     |

`init()` runs again when the user scrubs back via Left arrow, or when a
looping timeline restarts. **Anything that should reset on re-entry
goes in `init()`, not `setup()`** — typical examples: re-applying the
texture palette, re-seeding RNG, zeroing accumulators. Buffer
allocation belongs in `setup()` so it happens exactly once.

`shutdown()` only runs on a clean exit. Don't rely on it for anything
that must run during a scene transition.

---

## Step 3 — read the render context

Every `render()` receives a `const RenderContext *ctx`:

```c
typedef struct {
    unsigned char *backbuffer;
    unsigned int   frame;          /* frames since this scene started */
    unsigned long  ms;             /* ms     since this scene started */
    unsigned int   timeline_frame; /* frames since the timeline started */
    unsigned long  timeline_ms;    /* ms     since the timeline started */
} RenderContext;
```

- **`backbuffer`** is a 320×200 byte array (one byte per pixel as a
  palette index) that the engine hands you each frame. Most scenes
  draw into it and blit it once at the end of the frame to avoid
  tearing — but it's just a convenience, not a requirement. You can
  ignore it and write directly to `VGA_MEM` if a scene calls for it.
- **`frame`** is the canonical animation source. 8-bit truncation
  (`(unsigned char)ctx->frame`) gives a 0..255 wrap suitable for `sin8`
  / `cos8`.
- **`ms`** is for tweens and other ms-paced timelines.
- **`timeline_*`** is for music sync — see [music-sync.md](music-sync.md).

---

## Step 4 — draw, vsync, blit

VGA Mode 13h: 320×200, 8-bit indexed colour, single 64 KB framebuffer
at `0xA0000`. The engine doesn't double-buffer the hardware page; the
common pattern is to draw into a backbuffer in main RAM and `memcpy`
it into VGA memory once per frame, after a vsync. Direct writes to
`VGA_MEM` work too — `vga_putpixel`, `vga_clear`, and your own loops
are all valid — but every direct write races the beam and can tear if
it lands during scanout.

The render skeleton:

```c
static void render(const RenderContext *ctx)
{
    unsigned char *backbuffer = ctx->backbuffer;

    /* (1) Clear or paint over previous frame. */
    memset(backbuffer, 0, VGA_SIZE);

    /* (2) Draw whatever the scene draws. Pixels are palette indices. */
    backbuffer[100 * VGA_WIDTH + 160] = 255;       /* one pixel       */
    font_draw(&font_default, backbuffer, 4, 4, 255,
              "starfield.c");                       /* debug label    */

    /* (3) Wait for the CRT retrace, then copy to VGA. */
    vga_vsync();
    vga_blit(backbuffer);
}
```

What each step does:

1. **Clear**: the engine never clears the backbuffer for you — last
   frame's content is still there at the start of the next frame, and
   *the previous scene's last frame is still there at the start of a
   new scene*. Either `memset(backbuffer, 0, VGA_SIZE)` or paint over
   the whole frame (full-screen plasma, raycaster) — but skip the
   clear, never both. The cross-scene persistence is what makes
   snapshot-based scene transitions possible without engine help; see
   [Scene transitions](#scene-transitions) below.
2. **Draw**: write palette indices. The actual on-screen colour is
   determined by the DAC palette, which `init()` (or `setup`) sets up.
   Index `0` is whatever the palette says — usually black, sometimes
   not.
3. **`vga_vsync()`**: blocks until the CRT retrace begins. The blit
   that follows then runs entirely during the blanking interval, so
   the user never sees a half-drawn frame (no tearing).
4. **`vga_blit(backbuffer)`**: a `memcpy` of 64000 bytes from the
   backbuffer into VGA memory.

The vsync ↔ blit order matters. Calling `vga_blit` before
`vga_vsync` lets the blit race the beam and you'll see horizontal
tearing across the frame.

`vga_clear(color)` is also available — it's a `memset` straight to VGA
memory. Use it only if you want a flash effect that doesn't go through
the backbuffer; for normal rendering, clear the backbuffer instead.

---

## Step 5 — wire it into the timeline

In [src/demo.c](../src/demo.c) add the include and a `TimelineEntry`:

```c
#include "scenes/starfield.h"

TimelineEntry demo_timeline[] = {
    /* …existing entries… */
    {&starfield_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};
```

`XM_MS` derives the duration from tracker rows so the scene boundary
lands on a musical beat — see [music-sync.md](music-sync.md#step-4--derive-scene-durations-from-tracker-timing).
A duration of `0` runs the scene until the user hits ESC (used for
stand-alone demos, never inside a real timeline).

---

## Common things you'll want

### Palette

The DAC has 256 RGB triplets, each channel 0..63. Set entries
individually via `vga_setpalette(idx, r, g, b)`, or upload a whole
`Palette` ([palette.h](../src/scenes/utils/palette.h)) at once with
`palette_apply(&pal)`.

If your scene uses an asset with its own palette (a textured cube, a
loaded BMP), apply it in `init()`:

```c
static void init(const RenderContext *ctx)
{
    (void)ctx;
    palette_apply(&texture->palette);
}
```

Doing it in `setup()` works once, but the next scene's `init` will
overwrite the DAC, and when this scene comes back the colours will be
wrong.

### Debug label

The example scenes draw their source file name in the top-left corner —
a convention that makes it easy to identify what you're looking at while
scrubbing through the demo. It's optional; drop it for the final release.

```c
font_draw(&font_default, backbuffer, 4, 4, 255, "starfield.c");
```

### Scene transitions

Last scene's frame is still in the backbuffer when yours wakes up.
Snapshot it once, composite for the first N ms, done:

```c
#include "utils/dither.h"

static unsigned char prev[VGA_SIZE];
static int captured;

static void init(const RenderContext *ctx) { (void)ctx; captured = 0; }

static void render(const RenderContext *ctx)
{
    unsigned char *backbuffer = ctx->backbuffer;
    if (!captured) { memcpy(prev, backbuffer, VGA_SIZE); captured = 1; }

    /* ... draw your scene into backbuffer ... */

    if (ctx->ms < 500) {
        int t = (int)(ctx->ms * 256 / 500), x, y;
        for (y = 0; y < VGA_HEIGHT; y++) {
            const unsigned char *pat = dither_voidcluster8x8 + ((y & 7) << 3);
            unsigned char *row = backbuffer + y * VGA_WIDTH;
            for (x = 0; x < VGA_WIDTH; x++)
                if ((int)pat[x & 7] >= t)
                    row[x] = prev[y * VGA_WIDTH + x];
        }
    }
    vga_vsync(); vga_blit(backbuffer);
}
```

Variations: pixel-lerp fade, slide-over (offset blit), other matrices
from [dither.h](../src/scenes/utils/dither.h). The engine stays out;
each scene owns its own intro.

The snapshot is palette indices, so they paint in the *new* DAC during
the window — fine if both scenes share a palette, awkward otherwise.
Two outs:

- Capture the outgoing palette at the **top** of `init()` with
  `palette_read()` (before applying your own), then `palette_lerp`
  the DAC between captured and current during the window.
- Skip pixel snapshots entirely and fade through black via
  `palette_fade`.

#### Chaining scenes

For more elaborate composition — running another scene's render() as
a sub-pass and compositing the result — push a render target before
calling the sub-scene's `render()` and pop after:

```c
static unsigned char layer[VGA_SIZE];

static void render(const RenderContext *ctx)
{
    /* Sub-scene draws into `layer` instead of VGA. vga_vsync() inside
     * its render() becomes a no-op while the target is active. */
    vga_push_render_target(layer);
    sub_scene.render(ctx);
    vga_pop_render_target();

    /* Composite `layer` with our own content into ctx->backbuffer ... */

    vga_vsync();
    vga_blit(ctx->backbuffer);
}
```

The target stack is 4 deep, so chains can nest a few levels (a wrapper
calling a wrapper calling a base scene). Pushes beyond that are
silently ignored.

### Asset loading

Drop files in [`asset-sources/`](../asset-sources/) (PNG, OBJ — converted at
build time) or [`assets/`](../assets/) (BMP, MDL, FFT, FNT, XM — packed as-is).
The packer regenerates `src/assets.h` with `ASSET_*` constants. Loaders
live in [bitmap.h](../src/scenes/utils/bitmap.h),
[model.h](../src/scenes/utils/model.h), [font.h](../src/scenes/utils/font.h),
[fft.h](../src/scenes/utils/fft.h).

Load in `setup()`, free in `shutdown()`:

```c
static Bitmap *image;

static void setup(void)    { image = bitmap_load(ASSET_JML_BMP); }
static void shutdown(void) { bitmap_free(image); image = NULL; }
```

### Half-resolution rendering

For scenes whose per-pixel work is expensive (raytracing, software
shading, dense procedural fills), rendering into a 160×100 buffer and
pixel-doubling to 320×200 keeps the same screen coverage with a
quarter as many source pixels (160 × 100 = 16000 vs 320 × 200 =
64000):

```c
static unsigned char small[VGA_HALF_SIZE];

static void render(const RenderContext *ctx)
{
    /* expensive per-pixel work into the half-res buffer */
    raytrace_into(small);

    /* expand each source pixel into a 2x2 block in the backbuffer */
    vga_blit_2x_to_buffer(small, ctx->backbuffer);

    /* full-resolution overlays still draw normally */
    font_draw(&font_default, ctx->backbuffer, 4, 4, 255, "scene.c");

    vga_vsync();
    vga_blit(ctx->backbuffer);
}
```

`VGA_HALF_WIDTH` (160), `VGA_HALF_HEIGHT` (100), and `VGA_HALF_SIZE`
(16000) come from [vga.h](../src/engine/vga.h) for declaring the
source buffer. Each source pixel becomes a 2×2 block on screen, so the
visible result has chunkier edges — that's the tradeoff.

Pass `(unsigned char *)VGA_MEM` as the destination if you'd rather
write the upscaled image straight to VGA memory; the backbuffer route
is the more common choice because it leaves room for full-resolution
overlays before the final blit.

For 3D scenes the same trick works, but the rasterizers in
[render3d.c](../src/scenes/utils/render3d.c) hard-code `VGA_WIDTH` as
the destination stride. Render with halved camera parameters into the
upper-left 160×100 region of a `VGA_WIDTH * VGA_HALF_HEIGHT`-sized
work buffer, then call `vga_blit_2x_strided(work_buf, VGA_WIDTH,
ctx->backbuffer)` to read just that region. See
[spheremap.c](../src/scenes/spheremap.c) for a worked example.

### Interleaved rendering

For per-pixel work that's expensive but tolerates a bit of temporal
softness — slow-moving smoke, generative ambient effects, anything
that blurs into itself across frames — render only every other
scanline each frame and use `vga_blit_rows` to push just those lines:

```c
static void render(const RenderContext *ctx)
{
    int parity = ctx->frame & 1;        /* alternate odd/even rows */
    int y;

    /* Touch only the parity-matching scanlines this frame. */
    for (y = parity; y < VGA_HEIGHT; y += 2)
        render_scanline(ctx->backbuffer + y * VGA_WIDTH, y, ctx->frame);

    vga_vsync();

    /* Push only the lines we touched. */
    for (y = parity; y < VGA_HEIGHT; y += 2)
        vga_blit_rows(ctx->backbuffer, y, 1);
}
```

Per-pixel work drops to half (100 scanlines × 320 pixels = 32000
source pixels per frame instead of 64000) and the blit moves 32000
bytes to VGA memory instead of 64000 — the rows you didn't touch
stay on screen from the previous frame's blit. Each line refreshes
every other frame, so the trick works best for content that's
already smooth across frames; fast motion picks up a comb-like
temporal artifact for the same reason interlaced video does.

`vga_blit_rows(buf, y_start, y_count)` accepts any contiguous row
range and clamps out-of-range arguments. The same call covers
half-screen and letterbox patterns, e.g.
`vga_blit_rows(backbuffer, 50, 100)` pushes rows 50..149.

### Backbuffer-sized scratch buffers

For perf-critical scenes that read the backbuffer alongside one or two
LUTs in lockstep, allocate the LUTs at staggered cache offsets so they
land in different L1 sets:

```c
my_lut    = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_SCENE_0);
my_other  = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_SCENE_1);
/* ... */
mem_free_aligned(my_lut);
mem_free_aligned(my_other);
```

See [mem.h](../src/utils/mem.h) for slot constants and [plasma.c](../src/scenes/plasma.c) for
a worked example. For most scenes, plain `malloc` is fine.

### Running just your scene

```sh
./run.sh 5    # runs only scene index 5 (0-based) from demo_timeline
```

Useful while iterating — boots straight into the scene under test
instead of waiting through the prefix.

---

## Common pitfalls

- **Drawing without clearing.** Garbage from the previous frame's
  backbuffer (or even from another scene that ran in the same
  backbuffer slot) bleeds through. Either `memset` to background or
  paint over the whole frame.
- **`vga_blit` without `vga_vsync`.** Visible tearing. The pair must
  be called every frame, in order.
- **Mixing backbuffer draws with direct `VGA_MEM` writes mid-frame.**
  Each is fine on its own; combining them — drawing some pixels via
  the backbuffer and others straight to VGA in the same frame —
  usually means the direct writes show up one frame ahead of the
  blit, so they flicker. Pick one strategy per frame.
- **Allocating in `init()` instead of `setup()`.** `init()` runs every
  scene re-entry — you'll leak a buffer per cycle. Allocate once in
  `setup()`.
- **Forgetting `palette_apply` in `init()`.** Looks correct on the first
  pass, then the colours are wrong after a scene transition. The DAC is
  shared global state.
- **Per-frame `malloc` in `render()`.** Spikes garbage-collection-style
  costs and fragments the heap. All buffers should come from `setup()`.
- **Reading `ctx->frame` outside `render()`.** It's only valid during
  the call. Don't stash the pointer — copy what you need.
- **Writing past `VGA_SIZE`.** No bounds checking on the backbuffer
  pointer. A buggy inner loop can clobber heap or stack and will look
  random. The font and rasterizers clip to 320×200; raw `*dst++` loops
  do not.

---

## Reference scenes

| Scene                                            | Demonstrates                                                             |
| ------------------------------------------------ | ------------------------------------------------------------------------ |
| [plasma.c](../src/scenes/plasma.c)               | Full-screen procedural fill, cache-staggered LUTs, custom palette, tween |
| [tunnel.c](../src/scenes/tunnel.c)               | LUT-driven texture lookup, palette crossfade + FFT-driven flash          |
| [textured_cube.c](../src/scenes/textured_cube.c) | Smallest non-trivial 3D scene; minimal lifecycle                         |
| Any 3D scene                                     | Standard 3D pipeline (see [3d-graphics.md](3d-graphics.md))              |
