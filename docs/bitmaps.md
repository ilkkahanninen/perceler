# Bitmaps

8-bit indexed BMP is the engine's image format — one byte per pixel as a
palette index, 256 colours per palette, palette channels in VGA DAC
range (0..63). Index 0 is treated as transparent by the blitters.

The pipeline:

```
asset-sources/foo.png ──► tools/png2bmp.py ──► assets/foo.bmp ──► bitmap_load()
                          (build-time, auto)                      (in build/demo.dat)
```

You usually don't run `png2bmp.py` by hand — `make` invokes it on
every PNG in [asset-sources/](../asset-sources/) automatically. The interesting
question is **which palette ends up in the BMP**, because that decides
the on-screen colours and which textures can share a palette.

---

## Step 1 — drop a PNG in

```sh
cp my-image.png asset-sources/
make
```

Build picks up the new file, runs `png2bmp.py`, packs `assets/my-image.bmp`
into `build/demo.dat`, and regenerates `src/assets.h` with
`ASSET_MY_IMAGE_BMP`. Use it from a scene:

```c
static Bitmap *image;
static void setup(void)    { image = bitmap_load(ASSET_MY_IMAGE_BMP); }
static void shutdown(void) { bitmap_free(image); }
```

That's the whole easy path. The rest of this doc is about palette
control — when the easy path produces wrong colours, or when several
images need to coexist on the same DAC.

---

## Step 2 — pick a palette strategy

`png2bmp.py` resolves the output palette in this priority order:

| Priority | Trigger                      | Palette source                               |
| -------- | ---------------------------- | -------------------------------------------- |
| 1        | `-q` flag                    | Freshly quantised from this PNG (median cut) |
| 2        | `-p FILE`                    | Read from `FILE` (must be 8-bit indexed BMP) |
| 3        | Output `.bmp` already exists | Reused from the existing output BMP          |
| 4        | None of the above            | Freshly quantised from this PNG              |

The Makefile invokes `png2bmp.py` with no flags. So in practice:

- **First build**: PNG is quantised to a 255-colour palette via median
  cut (index 0 reserved for transparency).
- **Subsequent rebuilds after editing the PNG**: the palette is
  _preserved_ from the previous output — colours remap into the existing
  slots. This keeps palette indices stable across edits, which matters
  when a colormap LUT or hand-tuned scene logic depends on specific
  indices.

When you actually want a fresh quantisation (e.g. you've changed the
image dramatically and the old palette no longer fits), force it once:

```sh
python3 tools/png2bmp.py asset-sources/my-image.png assets/my-image.bmp -q
```

### Sharing a palette across multiple bitmaps

If two scenes show different bitmaps but should run on the same DAC
(e.g. a sprite sheet plus a background), point both at a shared
reference palette so the indices line up:

```sh
python3 tools/png2bmp.py asset-sources/sprite.png  assets/sprite.bmp  -p assets/landscape.bmp
python3 tools/png2bmp.py asset-sources/cursor.png  assets/cursor.bmp  -p assets/landscape.bmp
```

Override the Makefile's auto-call by adding an explicit rule for the
specific output, or by running `png2bmp.py` once manually before the
next `make` (the existing-output rule then picks up your palette
choice).

### Lambert-friendly palettes

For scenes that use `colormap_build()` to drive Lambert lighting on
textured geometry (see [2d-effects.md](2d-effects.md#palette-requirements)
for what can go wrong without it), `png2bmp.py` has a `--lambert MODE`
flag that organises the palette into per-hue ramps so shading neighbours
stay inside the same hue family:

```sh
python3 tools/png2bmp.py asset-sources/foo.png assets/foo.bmp -L hsv
python3 tools/png2bmp.py asset-sources/foo.png assets/foo.bmp -L ramps
```

Two modes — pick whichever looks better for the scene:

| Mode    | What it does                                     | When it fits                               |
| ------- | ------------------------------------------------ | ------------------------------------------ |
| `hsv`   | Bin pixels by hue, quantise each hue along the value (brightness) axis | Natural images that already span dark→light per hue (photos, painted textures). Stays close to source colours. |
| `ramps` | Pick dominant hues, synthesise clean black→hue→white ramps via HSL | Hand-painted textures with isolated accent colours. Doesn't preserve exact source colours but every hue gets a perfect ramp by construction. |

Both override the cached-output palette, so re-export with `-L` once
when the scene's lighting starts shading toward wrong hues. After that,
the cached palette takes over again on subsequent rebuilds (just like
plain quantisation).

`--lambert` is incompatible with `-p` — an external palette already
fixes the indices.

### Authoring a custom palette

For scenes that want a specific palette (a fade-friendly ramp, a
synesthetic colour scheme), edit [tools/make_palette.py](../tools/make_palette.py) and
regenerate:

```sh
python3 tools/make_palette.py asset-sources/palette.bmp
```

The output is a 16×16 8-bit indexed BMP where pixel `(x, y)` carries
palette index `y*16 + x`. Open it in GIMP / Aseprite as a custom
palette to paint art that lands directly in your chosen indices, then
pass `palette.bmp` via `-p` to `png2bmp.py`.

---

## Step 3 — use the bitmap at runtime

```c
static Bitmap *image;

static void setup(void)
{
    image = bitmap_load(ASSET_MY_IMAGE_BMP);
}

static void init(const RenderContext *ctx)
{
    (void)ctx;
    palette_apply(&image->palette);   /* upload BMP's palette to the DAC */
}

static void render(const RenderContext *ctx)
{
    memset(ctx->backbuffer, 0, VGA_SIZE);

    /* Blit onto the backbuffer (typical case). */
    bitmap_blit_to_buffer(image, ctx->backbuffer,
                          VGA_WIDTH, VGA_HEIGHT, 32, 16);

    vga_vsync();
    vga_blit(ctx->backbuffer);
}

static void shutdown(void) { bitmap_free(image); image = NULL; }
```

A `Bitmap` carries `width`, `height`, a flat top-to-bottom
`pixels[width * height]` byte array, and its own `Palette`. The loader
handles all the BMP byzantine bits — bottom-up scanlines, 4-byte stride
padding, BGRA palette ordering, conversion from 8-bit channels to the
DAC's 6-bit range — so you get a clean buffer.

### Two blitters

| Function                | Destination            | When to use                                  |
| ----------------------- | ---------------------- | -------------------------------------------- |
| `bitmap_blit_to_buffer` | Caller-supplied buffer | The common case — write into the backbuffer  |
| `bitmap_blit`           | VGA memory directly    | One-shot direct draws (splash screens, etc.) |

Both clip to the destination bounds and skip pixels with index 0 (the
transparent slot). Pick `bitmap_blit_to_buffer` for normal rendering;
`bitmap_blit` is there for cases where a scene chooses to bypass the
backbuffer.

---

## Recipes

### Splash screen / loaded image with its own palette

```c
static void init(const RenderContext *ctx) {
    (void)ctx;
    palette_apply(&image->palette);
}
```

Drop the PNG in, let `png2bmp.py` quantise, apply the palette in
`init()`. Single-image scenes get good colour fidelity for free.

### Sprite over a procedural background

The procedural pass owns the palette; the sprite is authored against it
(via `-p` referencing the same palette BMP). Apply the procedural
palette in `init()`, then blit the sprite into the backbuffer each
frame:

```c
bitmap_blit_to_buffer(sprite, ctx->backbuffer,
                      VGA_WIDTH, VGA_HEIGHT, x, y);
```

### Bitmap as a layer mask

The plasma scene blits a logo into a static `overlay` buffer once at
setup, then per-pixel composites it with the procedural fill: index 0
in the overlay falls through to plasma, anything else takes over. See
[plasma.c](../src/scenes/plasma.c).

### Manual conversions outside the build

```sh
python3 tools/png2bmp.py input.png output.bmp                # auto palette
python3 tools/png2bmp.py input.png output.bmp -q             # force fresh
python3 tools/png2bmp.py input.png output.bmp -p ref.bmp     # share palette
python3 tools/png2bmp.py input.png output.bmp -t 200         # alpha cutoff
```

`-t` controls the alpha threshold for transparency: pixels with
`alpha < threshold` become index 0. Default is 128 (50%).

---

## Common pitfalls

- **Forgetting `palette_apply` in `init()`.** The bitmap's pixel
  indices look right but the DAC is whatever the previous scene left
  behind, so colours come out wrong. Apply in `init()`, not just
  `setup()` — `init()` runs again on every scene re-entry.
- **Editing a PNG and getting unexpected colour shifts.** The build
  reused the _old_ palette and remapped your edits into existing
  slots. If that's wrong, regenerate with `-q` once, then let the
  cached palette take over again.
- **24-bit or compressed BMPs.** The loader only reads 8-bit
  uncompressed (`BI_RGB`). Other formats return `NULL`. The packed
  conversion always produces the right format, so this only bites if
  you hand-author a BMP elsewhere.
- **Treating index 0 as a real colour.** It's transparent — every
  blitter skips it. If the image has authored content at index 0,
  shift everything up by one (or use `-q` to let `png2bmp.py` reserve
  index 0 for you).
- **Two scenes fighting over the DAC.** If scene A's palette differs
  from scene B's, the transition will flicker for one frame. Either
  share a palette (`-p`) or accept the flicker.
- **Channels in 0..255 instead of 0..63.** The `Palette` struct stores
  6-bit-per-channel values for direct DAC upload; if you author a
  palette by hand (e.g. `vga_setpalette` in a `set_palette()`
  function), keep values in 0..63 (`make_palette.py` scales 0..63 ×
  4 to fit the BMP, then the loader shifts back).

---

## Reference

- API: [bitmap.h](../src/scenes/utils/bitmap.h), [palette.h](../src/scenes/utils/palette.h)
- Tools: [png2bmp.py](../tools/png2bmp.py), [make_palette.py](../tools/make_palette.py)
- Example scenes: [plasma.c](../src/scenes/plasma.c) (bitmap + procedural composite)
- File format: 8-bit indexed `BI_RGB` BMP, 256-entry BGRA palette,
  bottom-to-top scanlines padded to 4-byte stride
