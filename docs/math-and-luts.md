# Fixed-point math and lookup tables

The engine targets 486-class hardware where integer ops are cheaper
than floating-point and an FPU stall blocks the integer pipeline. So
the inner loops are integer-only: positions, normals, UVs, colours
all live as Q8.8 fixed-point ints, trig comes from a 256-entry
lookup, and randomness from an LCG. Float is fine at setup time — at
runtime, avoid it.

[math.h](../src/scenes/utils/math.h) defines the conventions.

---

## Q8.8 fixed-point

Values are signed 32-bit ints split into 8 integer + 8 fractional bits.
The bit pattern of `256` means `1.0`, `512` means `2.0`, `128` means
`0.5`, etc. Range: roughly ±127.996, with 1/256 ≈ 0.0039 precision.

| Macro / fn        | Meaning                                       |
| ----------------- | --------------------------------------------- |
| `FP_SHIFT` (= 8)  | Number of fractional bits                     |
| `FP_ONE` (= 256)  | The Q8.8 representation of 1.0                |
| `FP_HALF` (= 128) | 0.5                                           |
| `INT_TO_FP(x)`    | `x << 8` — encode integer `x`                 |
| `FP_TO_INT(x)`    | `x >> 8` — drop fractional part               |
| `FLOAT_TO_FP(x)`  | round-to-nearest from float (setup-time only) |
| `FP_MUL(a, b)`    | `(a * b) >> 8` via 32-bit intermediate        |
| `FP_DIV(a, b)`    | `(a << 8) / b`                                |

### Why ±127

`FP_MUL` widens to a 32-bit `long` for the intermediate, then shifts
back. As long as both operands stay in 16-bit range (i.e. real values
in roughly ±127), the product fits in 32 bits without overflow. Stay
inside that range for everything you'll multiply — positions, normals
(unit-length so they're already ≤ 1), UVs (in [0, 1]).

Larger values overflow silently. If you need world coordinates beyond
±127, scale them down before storage and back up after.

### Conversion happens at the boundary

```c
/* Setup-time: float in, Q8.8 stored. */
m->positions[i] = FLOAT_TO_FP(0.5f);

/* Runtime: integer multiply against another Q8.8 value. */
int x_world = FP_MUL(m->positions[i], scale);

/* Render-time: drop fractional bits to get a screen pixel. */
int sx = FP_TO_INT(x_world);
```

Don't sprinkle `FP_TO_INT` / `INT_TO_FP` through the inner loop — pick
one representation per variable and stick with it. Mixing is where the
bugs are: a `FP_MUL` with one Q8.8 and one plain int gives Q8.8 results
shifted right by 8, i.e. nonsense.

---

## Trigonometry

Angles are 8-bit, where `0..255` spans `0..2π`. The sine table is
unsigned bytes:

```c
extern const unsigned char sintab[256];   /* sintab[i] = 128 + 127*sin(i*π/128) */
```

For Q8.8 results, use the inline wrappers:

```c
int s = sin8(angle);   /* returns roughly -254..+254, Q8.8 sin */
int c = cos8(angle);   /* same as sin8(angle + 64) */
```

`sin8` does a table read, subtracts 128, and shifts left by 1. The
table stores `128 + 127*sin(...)` rounded to the nearest byte, so the
worst-case absolute error in the Q8.8 result is 1 (out of 256, i.e.
1/256 ≈ 0.4%) — fine for sub-pixel motion that ends up `>> 8`'d to a
screen coordinate.

### Frame-as-angle idiom

The 8-bit angle convention means animation rotation comes for free:

```c
unsigned char ay = (unsigned char)ctx->frame;        /* full revolution / 256 frames */
unsigned char ax = (unsigned char)(ctx->frame >> 1); /* half speed */
unsigned char az = (unsigned char)(ctx->frame * 3);  /* 3× speed (still wraps cleanly) */
```

Multiplying a frame counter by an integer and casting to `unsigned char`
gives a free wrapping rotation. No modulo, no float, no `% 360`.

### Higher precision

If you need angles finer than 256 steps per revolution, scale up:

```c
unsigned int fine_angle = ctx->frame << 4;          /* 4096 steps/turn */
int s = sin8((unsigned char)(fine_angle >> 8));     /* downsample for the table */
```

For most scenes the 256-step resolution is invisible — the render is
already a 320×200 pixel grid.

---

## Randomness

Three LCG-based generators sharing the same step (multiply, add,
write back):

```c
unsigned int seed = 0x12345678;          /* any non-zero starting value */

unsigned char  n = rand8(&seed);         /* 0..255 */
unsigned short s = rand16(&seed);        /* 0..65535 */
unsigned int   r = rand32(&seed);        /* full 32-bit */
```

The seed is `unsigned int` and updates in place. Each `rand*` call
advances the same state — they share the LCG, just slice different
widths from the high bits of the result.

### Determinism

Same seed, same sequence — every frame, every run, every machine. The
capture pipeline (`make capture`) relies on this. If you want
animation to be reproducible (and you do, for offline rendering), seed
once in `init()` from a fixed constant, not from `ctx->frame` or the
clock:

```c
static unsigned int rng_seed;
static void init(const RenderContext *ctx) {
    (void)ctx;
    rng_seed = 0xDEADBEEF;
}
```

If you want each frame's noise to differ but stay reproducible, fold
the frame number into the seed at the _top_ of the frame, then advance
the LCG within the frame:

```c
static void render(const RenderContext *ctx) {
    unsigned int s = 0xDEADBEEF ^ (ctx->frame * 2654435761u);
    /* ... draw with rand*(&s) ... */
}
```

---

## Cache-staggered allocations

For perf-critical scenes that read several full-screen buffers in
lockstep, the pointers' alignment within an L1 cache way matters. Two
buffers landing in the same cache set conflict-miss every iteration of
the inner loop. The `mem.h` helper allocates at controlled offsets so
you can keep them in different sets:

```c
backbuf  = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_BACKBUFFER);  /* slot 0  */
my_lut1  = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_SCENE_0);     /* slot 32 */
my_lut2  = mem_alloc_offset(VGA_SIZE, MEM_OFFSET_SCENE_1);     /* slot 64 */
```

Alignment is `MEM_ALIGN` (= 128 bytes); slot constants are spaced one
cache line (32 B) apart. Three slots are reserved for scene buffers
(`SCENE_0..2`). For most scenes, `malloc` is fine — only reach for
this when you're actually CPU-bound and `wdis` shows cache stalls in
the inner loop.

`mem_debug(ptr, "name")` prints the pointer, slot offset, and L1 set
index — call it before `vga_init()` to verify your buffers don't
collide. Free everything with `mem_free_aligned`, not `free`.

See [mem.h](../src/utils/mem.h) for the slot layout and [plasma.c](../src/scenes/plasma.c)
for a worked example.

---

## Pitfalls

- **Mixing Q8.8 and plain int.** `FP_MUL(q88_val, plain_int)` gives
  `q88_val * plain_int / 256`, not `q88_val * plain_int`. If the right
  operand is a plain int, multiply directly (`q88 * 3`) and skip the
  shift.
- **Q8.8 overflow.** Anything where intermediates exceed 16 bits per
  operand will overflow silently in `FP_MUL`. Audit world-space scale
  factors and translation vectors. Symptoms: model wraps around, signs
  flip.
- **`sin8(int_angle)` without the cast.** The wrappers take
  `unsigned char`. Passing a wider type compiles but truncates
  silently — usually what you want, but be explicit
  (`sin8((unsigned char)x)`) so the wraparound is visible.
- **Per-frame `srand()`-style reseeding.** Resetting the seed each
  frame freezes the noise. Advance the seed inside the frame, not on
  every frame boundary.
- **Calling `rand8(&seed)` with `seed` as a local.** That works for the
  current frame but you lose state on the next call. Either keep the
  seed in `static` storage, or derive it deterministically from
  `ctx->frame` so reproducibility holds.
- **Float in the inner loop.** Once a runtime path uses float
  arithmetic, the FPU is in the picture and you give up the
  integer-only invariant the engine is built around. Stay in int from
  `setup()` onward.

---

## Reference

- API: [math.h](../src/scenes/utils/math.h), [mem.h](../src/utils/mem.h)
- Reading order: [tween.h](../src/utils/tween.h) for higher-level keyframing,
  [render3d.c](../src/scenes/utils/render3d.c) for fixed-point in practice.
- Scenes: [plasma.c](../src/scenes/plasma.c) (sintab, dither thresholds),
  [tunnel.c](../src/scenes/tunnel.c) (sin-driven palette crossfade), most 3D
  scenes (Q8.8 throughout).
