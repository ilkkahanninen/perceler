# Perceler

A demo engine for DOS featuring real-time effects running in VGA Mode-X (320x240x256), with XM module playback through Sound Blaster 16. Cross-compiled from macOS/Linux using Open Watcom V2, runs in DOSBox-X.

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

| Target        | Description                                                |
| ------------- | ---------------------------------------------------------- |
| `make`        | Build everything                                           |
| `make assets` | Pack assets only (generates `demo.dat` and `src/assets.h`) |
| `make run`    | Build and launch in DOSBox-X                               |
| `make clean`  | Remove all build artifacts                                 |

## Project structure

```
src/
  main.c              Entry point, engine init/shutdown, scene selection
  demo.c/h            Demo definition: timeline and song
  engine/             Engine modules
    audio.c/h           XM playback (libxmp-lite + SB16)
    bitmap.c/h          8-bit indexed BMP loader
    data.c/h            Asset reader (reads from demo.dat)
    keyboard.c/h        Interrupt-driven keyboard handler
    modex.c/h           VGA Mode-X 320x240 graphics
    sb16.c/h            Sound Blaster 16 DMA driver
    scene.c/h           Scene system and timeline runner
    timer.c/h           PIT-based millisecond timer
  scenes/             Demo effects
    plasma.c/h          Sine-based plasma effect
    tunnel.c/h          Textured tunnel flythrough
  utils/              Shared utilities for scenes
    dither.h            Ordered dithering threshold maps (8x8)
    xmtiming.h          XM_MS() macro: BPM/speed/rows to milliseconds
assets/               Source asset files (BMP, XM)
tools/
  new_scene.sh          Generates boilerplate for a new scene
  pack_assets.py        Packs assets into demo.dat + generates assets.h
  make_palette.py       Generates palette preview BMP
```

## Adding new assets

1. Place the file in the `assets/` directory (e.g. `assets/logo.bmp`).

2. Run `make assets` (or just `make`). All files in `assets/` are packed automatically into `build/demo.dat`, and `src/assets.h` is regenerated with an `Asset` constant for each file:

```c
// Generated automatically in src/assets.h
static const Asset ASSET_LOGO_BMP = { 77878UL, 12345UL };
```

The constant name is the filename uppercased with dots and hyphens replaced by underscores, prefixed with `ASSET_`.

## Using bitmaps

Bitmaps must be 8-bit uncompressed BMP files (256-color indexed). The engine loads the embedded palette and converts it to VGA DAC range (0-63) automatically.

```c
#include "../engine/bitmap.h"
#include "assets.h"

static Bitmap *logo;

static void my_init(void)
{
    logo = bitmap_load(ASSET_LOGO_BMP);
}

static void my_shutdown(void)
{
    bitmap_free(logo);
    logo = NULL;
}

static void my_render(unsigned int draw_page, unsigned char frame)
{
    /* Apply the bitmap's palette to the VGA DAC (do once, e.g. on first frame) */
    if (frame == 0)
        palette_apply(&logo->palette);

    /* Blit to screen centered; color index 0 is transparent */
    bitmap_blit(logo, (MODEX_WIDTH - logo->width) / 2,
                (MODEX_HEIGHT - logo->height) / 2, draw_page);
}
```

Key functions:

| Function | Description |
| --- | --- |
| `bitmap_load(asset)` | Load an 8-bit BMP from `demo.dat`. Returns `NULL` on error. |
| `palette_apply(&bmp->palette)` | Set all 256 VGA DAC entries from the bitmap's palette. |
| `bitmap_blit(bmp, x, y, page)` | Draw bitmap at (x, y) on the given page. Index 0 is transparent. Clips automatically. |
| `bitmap_free(bmp)` | Free a loaded bitmap. |

## Adding a new scene

Run the generator script to create the boilerplate:

```sh
./tools/new_scene.sh myeffect
```

This creates `src/scenes/myeffect.c` and `src/scenes/myeffect.h` with the standard `Scene` struct wired up and a basic Mode X render loop.

Then add it to the timeline in `src/demo.c`. Use `XM_MS(bpm, speed, rows)` from `utils/xmtiming.h` to calculate durations from XM timing parameters. Music offsets are computed automatically by `timeline_init()`.

```c
#include "scenes/myeffect.h"

TimelineEntry demo_timeline[] = {
    {&plasma_scene,   XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {&myeffect_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 2)},
    {&tunnel_scene,   XM_MS(BPM, SPEED, PATTERN_LEN * 4)},
    {0, 0, 0}};
```

New source files are picked up automatically by the Makefile.
