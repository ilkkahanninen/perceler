# Perceler

A demo engine for DOS running in VGA Mode 13h (320×200×256), with XM module playback through Sound Blaster 16 or Gravis Ultrasound. Cross-compiled from macOS/Linux with Open Watcom V2, runs in DOSBox-X.

## Features

- 60 fps timeline-driven scene runner
- Sound Blaster 16 and Gravis Ultrasound runtime drivers
- Bitmap loader (.png, .bmp)
- 3D model loader (.obj, .3ds) with flat / Gouraud / textured rasterizers and sphere-map UV helper
- Procedural Platonic solids with optional per-face extrusion
- Bitmap font, palette utilities, parameter tweens, blur, dithering, drawing primitives
- Music sync: discrete sample triggers and continuous FFT band-energy tracks
- Deterministic offline rendering to MP4

API reference for each subsystem lives in its `.h` file. Usage guides — how to choose between features and put them together — live in [docs/](docs/):

- [Setting up a new scene](docs/new-scene.md) — lifecycle, render context, backbuffer, vsync/blit, wiring into the timeline.
- [Bitmaps](docs/bitmaps.md) — PNG → BMP pipeline, palette strategies, sharing palettes, runtime blitters.
- [2D effects](docs/2d-effects.md) — primitives, text, blur, dither, palette crossfades and brightness colormaps.
- [3D graphics](docs/3d-graphics.md) — picking a mesh source, rasterizer, shading model; the standard render loop; performance notes.
- [Particles](docs/particles.md) — SoA pool, gravity/drag, attractors, point-cone emitter, colour ramps, music-driven bursts.
- [Fixed-point math & LUTs](docs/math-and-luts.md) — Q8.8 conventions, sin8 / sintab, randomness, cache-staggered allocations.
- [Music sync](docs/music-sync.md) — driving effects from frame counters, tweens, sample triggers, and FFT tracks; scene-relative vs timeline-relative time.

## Quick start

```sh
./setup.sh    # toolchain + libraries + optional deps
make run      # build + launch in DOSBox-X
```

`setup.sh` downloads Open Watcom V2 and libxmp-lite into the project, and prompts to install DOSBox-X, ffmpeg, xmp, and numpy via Homebrew (macOS) or your distro package manager (Linux). Python 3 and `curl` are the only host prerequisites.

Run a subset of scenes by index:

```sh
./run.sh 2 3
```

## Controls

| Key        | Action              |
| ---------- | ------------------- |
| ESC        | Quit                |
| Left/Right | Previous/next scene |

## Make targets

| Target         | Description                                        |
| -------------- | -------------------------------------------------- |
| `make`         | Build everything                                   |
| `make assets`  | Pack assets and regenerate `src/assets.h`          |
| `make run`     | Build and launch in DOSBox-X                       |
| `make capture` | Render a deterministic capture to `build/demo.mp4` |
| `make release` | Copy `demo.exe` + `demo.dat` to `release/`         |
| `make clean`   | Remove all build artifacts                         |

## Project layout

```
src/demo.c         Demo definition (timeline + song)
src/engine/        Audio, VGA, scene runner, capture, timer
src/scenes/        Demo scenes
src/scenes/utils/  Reusable helpers (bitmap, model, render3d, fft, font, ...)
src/utils/         Shared engine + scene helpers (mem, timing, tween)
assets/            Files packed into build/demo.dat as-is (BMP, MDL, FFT, FNT, XM)
asset-sources/     PNG and OBJ files auto-converted at build time
tools/             Asset/build/capture scripts
```

## Building a new demo

### 1. Add a scene

```sh
./tools/new_scene.sh myeffect
```

Creates `src/scenes/myeffect.{c,h}` with the `Scene` struct wired up. The Makefile picks up new files automatically.

### 2. Wire it into the timeline

`src/demo.c` lists scenes in playback order with their durations. `XM_MS(bpm, speed, rows)` from `utils/timing.h` derives durations from XM tracker timing.

```c
TimelineEntry demo_timeline[] = {
    {&intro_scene,    XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&myeffect_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&outro_scene,    XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};
```

`demo_song()` in the same file returns the `Asset *` to play (or `NULL` for silence).

### 3. Render with the context

Each `render(const RenderContext *ctx)` receives `backbuffer`, `frame`, `ms`, `timeline_frame`, `timeline_ms`. Scene-relative fields drive animation; timeline-relative fields drive music sync. Details in [src/engine/scene.h](src/engine/scene.h).

### 4. Add assets

Drop files in `assets/` (packed as-is) or `asset-sources/` (converted at build time):

| Source  | Output                                                           |
| ------- | ---------------------------------------------------------------- |
| `*.png` | 8-bit indexed BMP; palette reused from existing output BMP, or quantised fresh. Override with `tools/png2bmp.py -p palette.bmp` or `-q` to force re-quantisation. |
| `*.obj` | `.mdl` with per-vertex normals from `vn` or `s` smoothing groups |
| `*.3ds` | One `.mdl` per mesh (run `tools/3ds2model.py` manually)          |

The packer regenerates `src/assets.h` with `ASSET_*` constants. Loaders for each type live in [bitmap.h](src/scenes/utils/bitmap.h), [model.h](src/scenes/utils/model.h), [font.h](src/scenes/utils/font.h), and [fft.h](src/scenes/utils/fft.h).

Custom fonts can be authored visually with `tools/font.py` (template / build subcommands).

### 5. Music sync

Two ways to drive scenes from the song:

- **Discrete sample triggers.** Extract the frames where each XM instrument fires:

  ```sh
  python3 tools/xm_sample_frames.py assets/song.xm src/sync.h 8 16
  ```

  Read with `SampleTrack` from [utils/timing.h](src/utils/timing.h).

- **Continuous band energy.** Pre-render the song to a per-frame `.fft` track:

  ```sh
  ./tools/xm2wav.sh assets/song.xm build/song.wav
  python3 tools/wav2fft.py build/song.wav assets/song_kick.fft --low 60 --high 200
  ```

  Read with `fft_load` / `fft_at` from [fft.h](src/scenes/utils/fft.h).

### 6. Capture to video

```sh
make capture
```

Runs the engine in deterministic offline-render mode and encodes the result to `build/demo.mp4` via `ffmpeg`. Edit [`dosbox-capture.conf`](dosbox-capture.conf) to adjust the DOS-side environment (output stem, sample rate). The host-side MP4 path is overridable on the command line:

```sh
make capture CAPTURE_OUT=trailer.mp4
```

## VS Code

Install the recommended **C/C++ Extension Pack**. IntelliSense is preconfigured in `.vscode/c_cpp_properties.json`.

## Inspecting generated code

```sh
tools/wdis.sh src/scenes/myeffect.c
```

Builds a one-off object with debug info, runs `wdis -a`, writes `build/<name>.disasm` and opens it in VS Code.

## License

See [LICENSE](LICENSE). Free to use with attribution. Cannot be used to promote sexism, racism, homophobia, transphobia, fascism, imperialism, or apartheid.
