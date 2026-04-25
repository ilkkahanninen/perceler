# Perceler

A demo engine for DOS featuring real-time effects running in VGA Mode 13h (320x200x256), with XM module playback through Sound Blaster 16 or Gravis Ultrasound. Cross-compiled from macOS/Linux using Open Watcom V2, runs in DOSBox-X.

## Prerequisites

- Python 3 (for asset packing during setup and build)
- curl (for downloading toolchain and libraries during setup)
- On macOS, [Homebrew](https://brew.sh/) is recommended so `setup.sh` can install DOSBox-X, ffmpeg, and xmp automatically

## Setup

Run the setup script to download the toolchain, libraries, and runtime dependencies:

```sh
./setup.sh
```

This will:

- Download and install Open Watcom V2 cross-compiler into `tools/watcom/`
- Download libxmp-lite 4.7.0 into `libs/libxmp-lite/`
- Install DOSBox-X via Homebrew (macOS) or print instructions (Linux)
- Optionally install ffmpeg (used by `make capture`); prompts before installing
- Optionally install the FFT-analysis deps (xmp CLI + numpy) used by `tools/xm2wav.sh` and `tools/wav2fft.py`; prompts before installing
- Optionally install the commit-msg hook that enforces the prefix convention
- Pack the bundled assets via `make assets`

## VSCode setup

Install the recommended extensions when prompted, or manually install the **C/C++ Extension Pack** (`ms-vscode.cpptools-extension-pack`).

IntelliSense is pre-configured in `.vscode/c_cpp_properties.json` with the correct include paths, Watcom-specific keyword shims, and target defines. It should work out of the box after running `./setup.sh`.

## Building

```sh
make
```

This packs assets into `build/demo.dat`, compiles all sources, and links `build/demo.exe`.

## Running

```sh
make run
```

Or equivalently:

```sh
./run.sh
```

This builds the project and launches it in DOSBox-X.

To run only specific scenes by their 0-based index (loops automatically):

```sh
./run.sh 0 1
```

or:

```sh
make run DEMO_ARGS="0 1"
```

## Controls

| Key         | Action                 |
| ----------- | ---------------------- |
| ESC         | Quit                   |
| Left arrow  | Jump to previous scene |
| Right arrow | Jump to next scene     |

## Sound card selection

The engine picks an output driver at startup based on environment variables:

- If `ULTRASND` is set, the Gravis Ultrasound driver is tried first (stereo, 8-bit per channel, streamed to GF1 onboard RAM via DMA).
- Otherwise — or if GUS init fails — the Sound Blaster 16 driver is used (stereo, 16-bit, DMA channel 5; reads `BLASTER` for base/IRQ).

To switch cards in DOSBox-X, toggle `gus=true` / `sbtype=sb16` in your `dosbox-x.conf`. DOSBox-X normally exports `ULTRASND` in the guest environment when GUS is enabled.

The output sample rate is configurable at runtime via `PERCELER_RATE`:

```
set PERCELER_RATE=44100
demo.exe
```

Accepted range is 4000-44100 Hz; invalid or unset values fall back to 22050. The rate is threaded through libxmp, the SB16 DSP, the GUS GF1 frequency register, and the capture WAV header, so everything stays consistent.

## Capturing video

When `PERCELER_CAPTURE` is set in the DOS environment, the engine switches to a deterministic offline render: a virtual clock advances by exactly 1000/60 ms per frame, audio is rendered into a WAV file via libxmp (not the hardware driver), and the framebuffer is streamed to a companion `.RAW` file. Output speed depends on how fast DOSBox-X can emulate the frame — audio/video stay in lockstep regardless.

Run the whole pipeline (build → capture → encode) with:

```sh
make capture
```

That runs DOSBox-X against [dosbox-capture.conf](dosbox-capture.conf) (a sibling of `dosbox-x.conf` whose `[autoexec]` sets `PERCELER_CAPTURE` and `PERCELER_RATE` and skips the trailing `pause`), then feeds the resulting `build/CAPTURE.RAW` + `build/CAPTURE.WAV` to [tools/capture2video.py](tools/capture2video.py) to produce `build/demo.mp4`. Requires `ffmpeg` on PATH.

To change the output stem or sample rate, edit `dosbox-capture.conf` directly. The host-side MP4 path is overridable via `make`:

```sh
make capture CAPTURE_OUT=trailer.mp4
```

Then encode directly:

```sh
python3 tools/capture2video.py --scale 3 build/CAPTURE build/demo.mp4
```

Scene navigation (left/right arrows) is disabled during capture so the output is always reproducible. ESC still aborts.

## Make targets

| Target         | Description                                                |
| -------------- | ---------------------------------------------------------- |
| `make`         | Build everything                                           |
| `make assets`  | Pack assets only (generates `demo.dat` and `src/assets.h`) |
| `make run`     | Build and launch in DOSBox-X                               |
| `make capture` | Build, render a deterministic capture, encode to `build/demo.mp4` |
| `make release` | Build and copy `demo.exe` + `demo.dat` to `release/`       |
| `make clean`   | Remove all build artifacts                                 |

## Project structure

```
src/
  demo.c/h            Demo definition: timeline and song
  engine/             Engine modules
    main.c              Entry point, engine init/shutdown, scene selection
    audio.c/h           XM playback; dispatches to SB16 or GUS at runtime
    data.c/h            Asset reader (reads from demo.dat)
    keyboard.c/h        Keyboard handler
    vga.c/h             VGA Mode 13h 320x200 graphics
    sb16.c/h            Sound Blaster 16 DMA driver
    gus.c/h             Gravis Ultrasound (GF1) driver, stereo 8-bit via DRAM DMA
    capture.c/h         Offline-render sink (video .RAW + audio .WAV) for video export
    scene.c/h           Scene system and timeline runner
    timer.c/h           Millisecond timer
  scenes/             Demo effects
    model_viewer.c/h    3D model viewer
    plasma.c/h          Plasma effect
    polyhedra.c/h       Rotating wireframe gallery of extruded Platonic solids
    tunnel.c/h          Tunnel
    utils/              Shared utilities for scenes
      bitmap.c/h          8-bit indexed BMP loader
      blur.c/h            Blur effects
      dither.h            Ordered dithering threshold maps (8x8) and dither_threshold()
      draw.c/h            Drawing primitives
      font.c/h            Bitmap text renderer; arbitrary width×height glyphs, built-in 8x8 font
      math.c/h            Sine table, 8.8 fixed-point arithmetic, sin8/cos8, SWAP etc.
      model.c/h           3D model loader (custom format)
      palette.c/h         Palette utilities incl. fade to black/white/color and crossfade between two palettes
      polyhedron.c/h      Procedural Platonic solids (tetra/cube/octa/icosa) with per-face extrusion
  utils/              Shared engine/scene utilities
    mem.c/h             Cache-staggered aligned allocator, MEM_OFFSET_* slot system
    timing.h            BPM/speed/rows to milliseconds, SampleTrack
    tween.c/h           Keyframe parameter tweening in Q8.8 fp, ms timeline
assets/               Source asset files (BMP, XM, MDL)
asset-sources/        Source files, converted during build
  palette.bmp           Reference palette for PNG conversion
  *.png                 PNG images, converted to 8-bit BMP
  *.obj                 3D models, converted to binary .mdl
tools/
  new_scene.sh          Generates boilerplate for a new scene
  obj2model.py          Converts Wavefront .obj to binary .mdl format
  3ds2model.py          Converts Autodesk .3ds to .mdl (one file per mesh)
  pack_assets.py        Packs assets into demo.dat + generates assets.h
  png2bmp.py            Converts PNG to 256-color indexed BMP
  make_palette.py       Generates palette preview BMP
  font.py               Creates font-drawing templates and compiles them to .fnt assets
  capture2video.py      Encodes a Perceler capture (.RAW + .WAV) to MP4 via ffmpeg
  xm_sample_frames.py   Extracts sample trigger frame numbers from XM files
  wdis.sh               Disassembles the .obj for a given .c file, source-annotated
```

## Adding new assets

Place source files in `asset-sources/` — they are converted automatically during build:

- `.png` files are converted to 8-bit indexed BMP using the palette from `asset-sources/palette.bmp`
- `.obj` files are converted to binary `.mdl` (8.8 fixed-point, fan-triangulated)

Autodesk `.3ds` files are not auto-wired into the build because a single `.3ds` can produce many `.mdl` outputs (one per mesh). Run the converter manually and drop the results into `assets/`:

```sh
python3 tools/3ds2model.py asset-sources/scene.3ds assets/
```

That writes one file per mesh, named `<input_stem>_<mesh_name>.mdl`.

### Custom fonts

The engine ships with a built-in 8x8 font (`font_default`). For custom styles, use `tools/font.py` to generate a drawing template, paint the glyphs in a pixel editor, then compile to a `.fnt` asset:

```sh
# 1. Generate an empty template (any multiple-of-8 width, any height)
python3 tools/font.py template --width 16 --height 16 mystyle.bmp

# 2. Open mystyle.bmp in a pixel editor and paint each glyph canvas using
#    palette index 1 (white). The gray borders/labels/reference glyphs are
#    ignored by the next step.

# 3. Compile to .fnt and drop into assets/
python3 tools/font.py build --width 16 --height 16 mystyle.bmp assets/mystyle.fnt
```

Load it at runtime with `font_load(ASSET_MYSTYLE_FNT)` and pair with `font_free()`.

### Packing

Files placed directly in `assets/` are packed as-is. All assets end up in `build/demo.dat`, and `src/assets.h` is regenerated with `ASSET_*` constants (filename uppercased, dots/hyphens become underscores).

See header files for API usage: `bitmap.h`, `model.h`, `math.h`, `draw.h`, `fft.h`, `font.h`, `palette.h`, `polyhedron.h`, `tween.h`.

## Syncing effects to music

Use `xm_sample_frames.py` to extract frame numbers (at 60 fps) where specific samples are triggered in an XM file:

```sh
# List all instruments in the module
python3 tools/xm_sample_frames.py assets/J9_THGHT.XM --list

# Generate a C header with frame arrays for samples 8 and 16
python3 tools/xm_sample_frames.py assets/J9_THGHT.XM src/sync.h 8 16
```

The generated header contains a `sample_N_frames[]` array and `SAMPLE_N_FRAMES_COUNT` define for each requested sample. The tool tracks XM speed/tempo changes for accurate timing.

To react to triggers in a scene, use `SampleTrack` from `utils/timing.h`:

```c
#include "utils/timing.h"
#include "sync.h" // generated header

static SampleTrack kick = SAMPLE_TRACK(sample_8_frames, SAMPLE_8_FRAMES_COUNT);

static void my_render(const RenderContext *ctx)
{
  if (sample_triggered(&kick, ctx->timeline_frame))
    /* kick drum hit — flash, shake, etc. */;
}
```

`sample_triggered()` returns 1 once per trigger when `ctx->timeline_frame` reaches the next entry in the array. `RenderContext` (defined in `engine/scene.h`) carries the backbuffer plus four time fields — `frame`/`ms` (since this scene started) and `timeline_frame`/`timeline_ms` (absolute position in the full demo). The `timeline_*` values match the frame numbers in the generated header.

### FFT band track

For continuous music-reactive effects (intensity that follows a frequency band rather than discrete sample triggers), pre-render the song to a per-frame band-energy track:

```sh
# 1. XM -> WAV via the xmp CLI (44.1 kHz stereo by default)
./tools/xm2wav.sh assets/J9_THGHT.XM build/song.wav

# 2. WAV -> 8-bit-per-frame energy track (60 fps, kick band)
python3 tools/wav2fft.py build/song.wav assets/song_kick.fft --low 60 --high 200
```

`wav2fft.py` peak-normalises the WAV, computes a windowed FFT for every frame (60 fps default), sums bin magnitudes inside `[--low, --high]` Hz, then re-normalises across the whole track to 0..255 — one byte per frame, no header. File length equals the frame count.

Drop the resulting `.fft` into `assets/` and read it from a scene with `fft.h`:

```c
#include "utils/fft.h"

static FFTTrack *kick;

static void my_setup(void) { kick = fft_load(ASSET_SONG_KICK_FFT); }
static void my_shutdown(void) { fft_free(kick); }

static void my_render(const RenderContext *ctx)
{
  unsigned char e = fft_at(kick, ctx->timeline_frame);
  /* drive intensity, brightness, scale, etc. from `e` (0..255) */
}
```

Stack multiple bands (e.g. one for bass, one for hats) into separate `.fft` files and load them as separate tracks.

## Adding a new scene

Run the generator script to create the boilerplate:

```sh
./tools/new_scene.sh myeffect
```

This creates `src/scenes/myeffect.c` and `src/scenes/myeffect.h` with the standard `Scene` struct wired up and a basic render loop.

Then add it to the timeline in `src/demo.c`. Use `XM_MS(bpm, speed, rows)` from `utils/timing.h` to calculate durations from XM timing parameters. Music offsets are computed automatically by `timeline_init()`.

```c
#include "scenes/myeffect.h"

TimelineEntry demo_timeline[] = {
    {&plasma_scene,   XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&myeffect_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&tunnel_scene,   XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};
```

New source files are picked up automatically by the Makefile.

The song is chosen by `demo_song()` in `src/demo.c`, which returns a pointer to an `Asset` from `src/assets.h`. Return `NULL` to run the demo silently.

```c
const Asset *demo_song(void)
{
  return &ASSET_J9_THGHT_XM;  /* or NULL for no music */
}
```

## Inspecting generated code

`tools/wdis.sh` disassembles the object for a given `.c` file with source lines interleaved as comments, useful for checking how the compiler scheduled a hot loop or spotting missed optimizations.

```sh
tools/wdis.sh src/scenes/utils/blur.c
```

The script rebuilds the object with line-number debug info (`-d1`), runs `wdis -a` for clean assembleable output (no hex byte dump), writes the result to `build/<name>.disasm`, and opens it in VS Code via the `code` CLI.

Inside VS Code there's also a task **Disassemble current file** (Cmd+Shift+P → Run Task). To get a split view with source on the left and disassembly on the right, add this to your personal `keybindings.json`:

```json
{
  "key": "cmd+alt+d",
  "command": "runCommands",
  "args": {
    "commands": [
      "workbench.action.splitEditorRight",
      {
        "command": "workbench.action.tasks.runTask",
        "args": "Disassemble current file"
      }
    ]
  }
}
```

Syntax highlighting for `.disasm` files is picked up automatically if you install the recommended extension `13xforever.language-x86-64-assembly`.

## License

See [LICENSE](LICENSE). Free to use with attribution. Cannot be used to promote sexism, racism, homophobia, transphobia, fascism, imperialism, or apartheid.
