# Perceler

A demo engine for DOS featuring real-time effects running in VGA Mode 13h (320x200x256), with XM module playback through Sound Blaster 16. Cross-compiled from macOS/Linux using Open Watcom V2, runs in DOSBox-X.

## Prerequisites

- Python 3 (for asset packing and palette tools)
- [DOSBox-X](https://github.com/joncampbell123/dosbox-x) (for running the demo)
- curl (for downloading toolchain and libraries during setup)

## Setup

Run the setup script to download Open Watcom V2, libxmp-lite, and install DOSBox-X:

```sh
./setup.sh
```

This will:

- Download and install Open Watcom V2 cross-compiler into `tools/watcom/`
- Download libxmp-lite 4.7.0 into `libs/libxmp-lite/`
- Install DOSBox-X via Homebrew (macOS) or print instructions (Linux)

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

## Make targets

| Target         | Description                                                |
| -------------- | ---------------------------------------------------------- |
| `make`         | Build everything                                           |
| `make assets`  | Pack assets only (generates `demo.dat` and `src/assets.h`) |
| `make run`     | Build and launch in DOSBox-X                               |
| `make release` | Build and copy `demo.exe` + `demo.dat` to `release/`      |
| `make clean`   | Remove all build artifacts                                 |

## Project structure

```
src/
  demo.c/h            Demo definition: timeline and song
  engine/             Engine modules
    main.c              Entry point, engine init/shutdown, scene selection
    audio.c/h           XM playback (libxmp-lite + SB16)
    data.c/h            Asset reader (reads from demo.dat)
    keyboard.c/h        Interrupt-driven keyboard handler
    vga.c/h             VGA Mode 13h 320x200 graphics
    sb16.c/h            Sound Blaster 16 DMA driver
    scene.c/h           Scene system and timeline runner
    timer.c/h           PIT-based millisecond timer
  scenes/             Demo effects
    model_viewer.c/h    Wireframe 3D model viewer with rotation
    plasma.c/h          Sine-based plasma effect
    tunnel.c/h          Textured tunnel flythrough
    utils/              Shared utilities for scenes
      bitmap.c/h          8-bit indexed BMP loader
      dither.h            Ordered dithering threshold maps (8x8) and dither_threshold()
      draw.c/h            Drawing primitives (Bresenham line)
      math.c/h            Sine table, 8.8 fixed-point arithmetic, sin8/cos8
      model.c/h           3D model loader (binary .mdl format)
      palette.c/h         Palette utilities (apply, lightness levels)
  utils/              Shared engine utilities
    timing.h            XM_MS() macro: BPM/speed/rows to milliseconds
assets/               Source asset files (BMP, XM, MDL)
asset-sources/        Source files, converted during build
  palette.bmp           Reference palette for PNG conversion
  *.png                 PNG images, converted to 8-bit BMP
  *.obj                 3D models, converted to binary .mdl
tools/
  new_scene.sh          Generates boilerplate for a new scene
  obj2model.py          Converts Wavefront .obj to binary .mdl format
  pack_assets.py        Packs assets into demo.dat + generates assets.h
  png2bmp.py            Converts PNG to 256-color indexed BMP
  make_palette.py       Generates palette preview BMP
```

## Adding new assets

Place source files in `asset-sources/` — they are converted automatically during build:

- `.png` files are converted to 8-bit indexed BMP using the palette from `asset-sources/palette.bmp`
- `.obj` files are converted to binary `.mdl` (8.8 fixed-point, fan-triangulated)

Files placed directly in `assets/` are packed as-is. All assets end up in `build/demo.dat`, and `src/assets.h` is regenerated with `ASSET_*` constants (filename uppercased, dots/hyphens become underscores).

See header files for API usage: `bitmap.h`, `model.h`, `math.h`, `draw.h`.

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

## License

See [LICENSE](LICENSE). Free to use with attribution. Cannot be used to promote sexism, racism, homophobia, transphobia, fascism, imperialism, or apartheid.
