# 2D effects and post-processing

The non-3D building blocks: drawing primitives, text, full-screen blur
and dither, palette crossfades and brightness colormaps. These compose
freely and most of them write into (or transform) a 320×200 backbuffer
of palette indices.

| Layer           | Functions                                              |
| --------------- | ------------------------------------------------------ |
| Primitives      | `draw_line`, `draw_triangle`                           |
| Bitmap blits    | `bitmap_blit_to_buffer` — see [bitmaps.md](bitmaps.md) |
| Text            | `font_draw`, `font_default`, `font_load`               |
| Full-frame blur | `blur`                                                 |
| Ordered dither  | `dither_threshold` + `dither_*` matrices               |
| Palette tricks  | `palette_lerp`, `palette_fade`, `colormap_build`       |

---

## Drawing primitives

[draw.h](../src/scenes/utils/draw.h) covers the two primitives the engine ships:

```c
draw_line(buf, x0, y0, x1, y1, color);
draw_triangle(buf, x0, y0, x1, y1, x2, y2, color);
```

`color` is a palette index (0..255). Both are clipped to 320×200, so
out-of-bounds coordinates are safe. For a wireframe, three `draw_line`
calls per triangle ([model_wireframe.c](../src/scenes/model_wireframe.c)). For solid 2D
shapes, `draw_triangle`. Anything more elaborate (circles, beziers,
filled polygons with >3 vertices) you write yourself.

For 3D-rasterized triangles with z-buffer, shading, or texturing, use
the `fill_triangle_*` family from [render3d.h](../src/scenes/utils/render3d.h)
instead — see [3d-graphics.md](3d-graphics.md).

---

## Text

The default font is built in: an 8×8 IBM-style font covering ASCII
32..127. No setup, no asset, just call:

```c
font_draw(&font_default, backbuffer, x, y, color, "hello");
```

`color` is a palette index. The blit is transparent — set bits become
`color`, unset bits leave the underlying pixel alone. Bounds-clipped to
320×200.

### Custom fonts

For a different look (a chunkier display font, a sci-fi font, etc.),
author one with [tools/font.py](../tools/font.py):

```sh
# 1. Create an empty 16×16 template BMP with grid + labels.
python3 tools/font.py template --width 16 --height 16 mystyle.bmp

# 2. Open mystyle.bmp in a pixel editor. Paint glyph pixels with
#    palette index 1 (white); leave borders / labels alone.

# 3. Build the .fnt asset.
python3 tools/font.py build --width 16 --height 16 mystyle.bmp \
        assets/mystyle.fnt
```

Drop the `.fnt` in `assets/`, rebuild, and load it like any other
asset:

```c
static Font *bigfont;
static void setup(void)    { bigfont = font_load(ASSET_MYSTYLE_FNT); }
static void shutdown(void) { font_free(bigfont); }
```

`glyph_w` must be a multiple of 8 (the format packs glyph rows LSB-first
in whole bytes). Common sizes: 8×8, 16×16, 24×32. See [font.h](../src/scenes/utils/font.h)
for the binary layout.

---

## Full-frame blur

[blur.h](../src/scenes/utils/blur.h) exposes one function:

```c
blur(backbuffer);   /* 3-tap (1,2,1)/4 separable, in-place */
```

The inner loop is SWAR-optimised (4 pixels per 32-bit word) and all
arithmetic happens on palette indices. It blurs the **index space**,
not RGB, so the output makes sense only when neighbouring indices
represent neighbouring colours — i.e. on a palette laid out as a
gradient (a black-blue-red-yellow ramp, a greyscale, etc.). On an
arbitrary unsorted palette, blurring 0x10 with 0x20 gives you 0x18,
which can be any colour.

When to use it:

- Smooth out aliasing on procedural fills (the plasma scene runs `blur`
  on every frame).
- Soften pixel-art bitmaps before compositing.
- As a setup-time pass on a LUT to soften lookups.

Don't use it when the palette is non-gradient (a textured BMP's
palette, the colormap'd scenes).

---

## Ordered dither

[dither.h](../src/scenes/utils/dither.h) provides three 8×8 threshold matrices and an
inline test. Per-pixel:

```c
unsigned char threshold = dither_bayer8x8[((y & 7) << 3) + (x & 7)];
output = (value > threshold) ? on_color : off_color;
```

or via the inline helper:

```c
if (dither_threshold(dither_bayer8x8, x, y, value)) ...
```

`value` is 0..255 (compare it against the matrix's 0..252 range). The
result is a noisy 1-bit yes/no per pixel that converges to the
threshold luminance when zoomed out.

### Picking a pattern

| Matrix                  | Visual          | Use for                                                                    |
| ----------------------- | --------------- | -------------------------------------------------------------------------- |
| `dither_bayer8x8`       | Recursive grid  | Classic dithered look.                                                     |
| `dither_cluster8x8`     | Clustered dots  | Halftone print look, good for banded gradients.                            |
| `dither_voidcluster8x8` | Blue-noise-like | Least visible artifacts; use when you want the dither itself to disappear. |

The plasma scene uses `dither_cluster8x8` to fade the logo overlay in
and out as the threshold sweeps with `frame`:

```c
if (bmp_pixel != 0 &&
    dither_threshold(dither_cluster8x8, x, y, threshold)) {
    *dst++ = bmp_pixel;
}
```

---

## Palette effects

[palette.h](../src/scenes/utils/palette.h) treats the DAC as a writable parameter — far cheaper
than touching pixels in the backbuffer.

### Crossfade between two palettes

```c
palette_lerp(&pal_current, &pal_warm, &pal_cool, t);  /* t = 0..256 */
palette_apply(&pal_current);
```

Swap mood without moving a pixel. `t = 0` → full warm, `t = 256` → full
cool, in between → linear blend. One pass over 256 entries.
[tunnel.c](../src/scenes/tunnel.c) crossfades on a sine timer for a continuous
pulse.

### Fade to a solid colour

```c
palette_fade(&pal_current, &pal_src, 0, 0, 0, t);    /* fade-to-black */
palette_fade(&pal_current, &pal_src, 63, 63, 63, t); /* fade-to-white */
palette_fade(&pal_current, &pal_src, 63, 0, 0, t);   /* fade-to-red flash */
```

Channel values are in DAC range (0..63, **not** 0..255). `t = 0` keeps
`src`, `t = 256` is the solid colour. Useful for transitions, kick
flashes, fade-ins from black.

### Brightness colormap (texture × lighting)

For textured scenes that want per-pixel Lambert without a per-pixel
multiply: precompute a table mapping `(brightness_level, texel_index) →
palette_index`.

```c
static Palette *texture_palette;
static Colormap *cm;

static void setup(void) {
    cm = malloc(sizeof(Colormap));
    colormap_build(cm, &texture_palette);
}

/* In the rasterizer: */
unsigned char shaded = cm->map[(level << 8) | texel];
```

`level` is 0..63 where 32 is the original palette, 0 is black, 63 is
white. The 16 KB table replaces "sample texel → scale by brightness →
find nearest palette entry" with one byte lookup per pixel: a single
indexed load using `(level << 8) | texel` as the address.

[shaded_cube.c](../src/scenes/shaded_cube.c) and
`fill_triangle_textured_gouraud` use this. The table costs 16 KB of
RAM. The build runs 64 × 256 = 16384 nearest-colour searches, so it
belongs in `setup()` — not the per-frame path.

#### Palette requirements

The colormap can only emit indices that **already exist** in the
source palette. `colormap_build` iterates every (level, texel) pair,
scales the texel's RGB toward 0 or 255 by the brightness level, and
then searches the palette for the nearest existing entry by squared
RGB distance — the result is whichever index wins.

That means the palette needs brightness ramps for every hue you want
to shade. The pathological case: mostly-blue palette plus one red
entry. At full brightness the red stays red, but at half-dark there's
no dark red in the palette, so the nearest entry is a dark blue —
the Lambert-shaded red surface drifts to blue as it turns away from
the light. Same artifact in the other direction without a light red.

Natural textures (marble, landscapes, photos) have broad colour
distributions and usually shade cleanly. Hand-painted palettes with
isolated accent colours don't. Three options when shading goes wrong:

- **Re-export the source PNG with `png2bmp.py -L hsv` or `-L ramps`** —
  builds a palette explicitly organised into per-hue brightness ramps.
  See [bitmaps.md](bitmaps.md#lambert-friendly-palettes) for the
  difference between the two modes.
- **Hand-author dark and light variants** of every accent colour in
  the palette so the colormap has somewhere to go.
- **Skip the colormap entirely** and do a per-pixel Lambert multiply
  in your own rasterizer.

### Levels-only (no texture)

`palette_calc_levels` builds the 64-level palette ladder without the
texture-index dimension. Useful if you want fade ramps but don't need
texture × lighting LUTs.

---

## Recipes

### Cinematic fade-in from black

```c
static void render(const RenderContext *ctx) {
    /* draw the scene normally into the backbuffer */
    /* ... */

    /* Then apply the fade to the DAC instead of pixels. */
    int t = (ctx->ms < 1000) ? (int)(ctx->ms * 256 / 1000) : 256;
    palette_fade(&pal_current, &pal_master, 0, 0, 0, 256 - t);
    palette_apply(&pal_current);

    vga_vsync();
    vga_blit(ctx->backbuffer);
}
```

Same idea works for fade-out (`t` decreases), white flash on a beat
(set t = energy from FFT), or coloured wash.

### Soft procedural

```c
/* Fill backbuffer with sintab-driven colour values, then soften. */
fill_full_screen(backbuffer);
blur(backbuffer);
```

### Dither-mask reveal

Use a rising threshold so a bitmap "fades in" pixel by pixel:

```c
unsigned char threshold = (unsigned char)(ctx->frame & 0xFF);
for (y = ...) {
    for (x = ...) {
        if (dither_threshold(dither_voidcluster8x8, x, y, threshold))
            backbuffer[y * VGA_WIDTH + x] = source_pixel;
    }
}
```

Pair `dither_voidcluster8x8` with this kind of monotone sweep — its
blue-noise pattern is the least visually distracting during the reveal.

### Half-resolution heavy effect

When per-pixel work is too expensive at 320×200, render into a
160×100 buffer and pixel-double on output:

```c
raytrace_into(small);                              /* fills VGA_HALF_SIZE bytes */
vga_blit_2x_to_buffer(small, ctx->backbuffer);     /* expand 2x into backbuffer */
```

See [new-scene.md](new-scene.md#half-resolution-rendering) for the
full pattern.

### Interleaved rendering

For effects that change slowly across frames, an alternative to
half-res is to write only every other scanline each frame and push
just those rows with `vga_blit_rows`:

```c
int parity = ctx->frame & 1;
int y;
for (y = parity; y < VGA_HEIGHT; y += 2)
    render_scanline(ctx->backbuffer + y * VGA_WIDTH, y);
for (y = parity; y < VGA_HEIGHT; y += 2)
    vga_blit_rows(ctx->backbuffer, y, 1);
```

See [new-scene.md](new-scene.md#interleaved-rendering) for the
tradeoffs.

---

## Common pitfalls

- **Blurring an arbitrary palette.** The output indices are
  meaningless unless the palette is gradient-ordered. If colours come
  out wrong, run blur _before_ applying the texture's palette, not
  after.
- **DAC channel range confusion.** `palette_fade` and `vga_setpalette`
  take 0..63 per channel. Passing 0..255 produces near-white results
  (the high bits get clipped or shifted).
- **`colormap_build` per frame.** It runs 64 × 256 nearest-colour
  searches over the palette — call it once in `setup()` against the
  palette you'll actually display. Re-build only if the palette
  changes meaningfully.
- **Mixing colormap'd output with `palette_lerp`.** The colormap is
  baked against the palette that was live when you built it. Crossfade
  to a different palette and the colormap'd pixels look wrong; rebuild
  the colormap or pick one approach per scene.
- **`draw_triangle` for 3D.** It has no z-buffer, no UVs, no lighting.
  Use it for 2D overlays only.

---

## Reference

- API: [draw.h](../src/scenes/utils/draw.h), [font.h](../src/scenes/utils/font.h),
  [blur.h](../src/scenes/utils/blur.h), [dither.h](../src/scenes/utils/dither.h),
  [palette.h](../src/scenes/utils/palette.h)
- Tools: [font.py](../tools/font.py)
- Scenes: [plasma.c](../src/scenes/plasma.c) (blur, dither, tween + bitmap),
  [tunnel.c](../src/scenes/tunnel.c) (palette crossfade + FFT-driven flash),
  [shaded_cube.c](../src/scenes/shaded_cube.c) (colormap)
