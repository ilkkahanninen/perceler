# Particles

A struct-of-arrays particle pool with Q8.8 world-space coordinates,
integer-only update and draw. Particles share the existing 3D pipeline
— same `Camera3D`, same `(angle_y, angle_x)` rotation convention as
`transform_points` — so they sit in the same world as a scene's mesh
content.

The per-frame cycle:

```
emit ─► update ─► draw
(spawn   (decay   (rotate by (ay, ax),
 from    life,    project, draw N×N
 wherever) integrate) block, clip)
```

API lives in [particles.h](../src/scenes/utils/particles.h). Worked
example: [particles.c](../src/scenes/particles.c).

---

## Step 1 — set up the pool

`particles_create(capacity)` allocates a fixed-capacity SoA pool.
Slots are reused as particles die — capacity needs to be at least
`max_concurrent_particles`, where concurrent count tops out at
roughly `spawn_rate * max_lifetime` for continuous emission.

```c
static ParticleSystem *ps;

static void setup(void)    { ps = particles_create(2000); }
static void shutdown(void) { particles_free(ps); ps = NULL; }
static void init(const RenderContext *ctx)
{
    (void)ctx;
    particles_clear(ps);   /* start every re-entry with an empty pool */
}
```

`particles_clear` resets every slot to dead without deallocating —
use it in `init()` so scrubbing back to the scene replays the
intro cleanly.

Per-slot memory cost is ~38 bytes (positions, velocities, life,
life_max, ramp, base, size). 2000 particles ≈ 76 KB.

---

## Step 2 — the per-frame cycle

The order is **emit → update → draw**. Update both decays life and
moves particles, so emitting after update means new particles render
at their spawn position; emitting before update means they take one
integration step before rendering. Either is fine; pick one and stick
with it.

```c
static void render(const RenderContext *ctx)
{
    unsigned char *backbuffer = ctx->backbuffer;
    unsigned char ay = (unsigned char)ctx->frame;
    unsigned char ax = (unsigned char)(ctx->frame >> 1);

    particles_emit(ps, /* spawn args */ );      /* (1) */
    particles_update(ps, &forces);              /* (2) */

    memset(backbuffer, 0, VGA_SIZE);
    particles_draw(ps, &camera, ay, ax, backbuffer);   /* (3) */

    vga_vsync();
    vga_blit(backbuffer);
}
```

The `(angle_y, angle_x)` you pass to `particles_draw` controls how
the particle world rotates relative to the camera, the same way
`transform_points` does. Pass the same angles you'd pass to a mesh's
transform if you want the particles to spin with the scene; pass `0,
0` for camera-locked dust that doesn't rotate with the world.

---

## Step 3 — pick forces

`ParticleForces` is uniform across the whole pool — every live
particle gets the same gravity vector and drag this frame.

```c
static const ParticleForces forces = {
    0,                       /* gx */
    -INT_TO_FP(1) / 24,      /* gy — pulls toward -y */
    0,                       /* gz */
    250                      /* drag — Q8.8 multiplier per frame */
};
```

| Field      | Meaning                                                      |
| ---------- | ------------------------------------------------------------ |
| `gx/gy/gz` | Q8.8 acceleration vector. Added to velocity each frame.      |
| `drag`     | Q8.8 multiplier on velocity per frame. `FP_ONE` (256) = none, `0` = instant stop, smaller-than-256 values bleed energy. |

Each frame, `particles_update` applies drag first, then adds gravity,
then integrates position. For position-dependent forces (a pull toward
a point, a swirl) use `particles_apply_attractor` — see the next
subsection.

### Attractors

`particles_apply_attractor(ps, x, y, z, strength)` adds a spring-like
force to every live particle's velocity:

```
delta_v = strength * (target - particle)
```

— Q8.8 throughout. Positive `strength` attracts; negative repels;
magnitude grows linearly with distance, so far-away particles feel a
stronger pull than nearby ones (it's a Hooke spring, not gravity).

Call once per attractor per frame, **before** `particles_update` so
the resulting velocity is fed through drag and position integration:

```c
unsigned char a = (unsigned char)(ctx->frame * 2);    /* orbit angle */
int ax = FP_MUL(INT_TO_FP(2), cos8(a));               /* radius 2.0  */
int az = FP_MUL(INT_TO_FP(2), sin8(a));
particles_apply_attractor(ps, ax, INT_TO_FP(1), az, FP_ONE / 32);
particles_update(ps, &forces);
```

Multiple attractors compose by summing — call the function once per
attractor in the same frame. Stationary attractors are just a constant
target; moving attractors derive their position from `ctx->frame` or a
tween.

Drag from `ParticleForces` is what keeps the spring stable: with
no damping, particles oscillate forever past the target. The 240–250
range that fits a fountain works for attractors too. A very large
`strength` paired with weak drag will produce visible oscillation —
turn one down or the other up.

[particles.c](../src/scenes/particles.c) keeps a small attractor orbiting
the spawn point in the horizontal plane, dragging the column of
embers into a swirl as it moves.

---

## Step 4 — pick spawn timing

The particle API exposes one primitive — `particles_emit` — and the
caller decides when to call it. Four common patterns:

### Continuous

```c
particles_emit(ps, 0, 0, 0, 0, INT_TO_FP(1)/4, 0,
               INT_TO_FP(1)/8,         /* spread (per-axis jitter) */
               60, 110,                /* life range, in frames */
               180, -180,              /* base colour, ramp */
               1,                      /* size: 1×1 pixel */
               6, &seed);              /* spawn 6 per call */
```

Call once per frame for a steady stream. Steady-state particle count
≈ `rate × avg_lifetime` — make sure the pool capacity covers that.

### Burst

A single one-off call with a high `count` and tighter timing
parameters — explosions, beat punctuation, scene-entrance flourish.

```c
particles_emit(ps, 0, 0, 0, 0, INT_TO_FP(1)/4, 0,
               INT_TO_FP(1)/2,         /* wider scatter */
               40, 90,
               220, -220,
               2,                      /* 2×2 blocks */
               200, &seed);
```

### Sample-trigger

Pair with `SampleTrack` from [timing.h](../src/utils/timing.h). The
trigger fires once per song event (kick, snare, lead-in stab — see
[music-sync.md](music-sync.md)).

```c
if (sample_triggered(&kick, ctx->timeline_frame))
    particles_emit(ps, /* burst args */ );
```

### FFT-driven

For one burst per audible peak in a band's energy stream, drive an
`OnsetDetector` from [fft.h](../src/scenes/utils/fft.h) — it filters
small bumps in the decay tail and enforces a cooldown so a single
slope can't burst on consecutive frames:

```c
static OnsetDetector snare_onset;

/* init() */
onset_init(&snare_onset, 100 /* floor */, 8 /* cooldown frames */);

/* render() */
int e = (int)fft_at(snare, ctx->timeline_frame);
if (onset_step(&snare_onset, e))
    particles_emit(ps, /* burst args */ );
```

See [music-sync.md](music-sync.md#from-band-energy-to-discrete-events-onsetdetector)
for tuning notes. Without the detector, a sustained-loud frame would
burst every frame and exhaust the pool.

---

## Step 5 — appearance

Each particle carries three render parameters set at spawn:

| Param     | Effect                                                        |
| --------- | ------------------------------------------------------------- |
| `base`    | Palette index at spawn time.                                  |
| `ramp`    | Signed step added across the full lifetime. Linear interpolation, saturated to 0..255. |
| `size`    | Render block edge in pixels. `1` = single pixel; `2` = 2×2; etc. |

The colour walk is `base + ramp * age / life_max`, where `age =
life_max - life`. So:

- `ramp = 0` keeps the colour constant for the whole lifetime.
- `ramp = -base` fades to black if the palette has black at index 0.
- `ramp = +N` brightens, useful when the palette runs cool→hot.

This works cleanly when neighbouring palette indices represent
neighbouring colours — the same gradient-palette assumption that
`blur` and `palette_lerp` lean on. On an unsorted palette the ramp
walks through arbitrary colours.

The per-axis `spread` parameter at emit time uniformly samples a
random offset in `[-spread, +spread]` for each of vx, vy, vz. Narrow
spread → pencil stream; wider spread → spray.

---

## Recipes

### Fire fountain

Continuous upward stream with gravity pulling back down — the
[particles.c](../src/scenes/particles.c) demo. `gy` negative, narrow spread,
ramp from a hot palette index toward black.

### Beat-locked puff

Edge-triggered burst on FFT energy or sample trigger. Wider spread,
larger size, steeper ramp so the flash is brief.

### Trail behind a moving object

Each frame, emit a few particles at the object's current position
with low or zero base velocity. Keep the spread small, lifetime
long-ish, ramp toward black. The trail length is `rate × lifetime`.

### Ambient drift (snow, dust)

Continuous low-rate emission in a wide volume, gentle gravity,
moderate drag. Pass `angle_y = angle_x = 0` to `particles_draw` so
the cloud doesn't spin with the world.

### Orbiting attractor

Move an attractor on a circle around the spawn point — particles
trail it like a comet's tail. Drive the orbit angle from `ctx->frame`
(8-bit truncation gives a free wrap; see
[math-and-luts.md](math-and-luts.md#frame-as-angle-idiom)). The
[particles.c](../src/scenes/particles.c) scene uses this pattern.

---

## Performance notes

- The update loop is a single linear scan over `capacity`. Dead slots
  cost one branch each; live slots cost the integration step.
- The draw loop projects per particle and then writes `size × size`
  pixels. Per-particle cost grows quadratically with `size` — large
  blocks at high counts dominate frame time.
- The spawn-side free-slot search uses a hint cursor that advances
  past each used slot, so amortised cost is constant unless the pool
  is nearly full.

If draw becomes the bottleneck, look at the half-resolution rendering
pattern in [new-scene.md](new-scene.md#half-resolution-rendering) —
particles draw cleanly into a half-res work buffer.

---

## Common pitfalls

- **Capacity smaller than `rate × lifetime`.** New spawns are silently
  dropped once the pool is full, so the visible particle count
  plateaus and never reaches what you'd expect from the spawn rate.
- **Forgetting to advance the seed.** `particles_emit` advances `*seed`
  in place. Passing the same seed value (e.g. a literal) every frame
  freezes the random offsets — particles spawn in the same scatter
  pattern every frame.
- **Calling `particles_clear` from `setup()`, not `init()`.** A scene
  re-entry doesn't re-run `setup`; without a clear in `init()` the
  pool keeps the dying particles from the previous run, which look
  like ghosts at the top of the scene.
- **Drag outside [0, 256].** The update multiplies velocity by drag
  in Q8.8. Values above 256 amplify velocity each frame (particles
  accelerate spontaneously); values below 0 flip the sign.
- **`particles_draw` with the wrong rotation angles.** If your scene
  uses `transform_points(..., ay, ax, cam_z)` for its meshes but
  passes different angles to `particles_draw`, the particles drift
  visually away from the geometry.
- **N×N blocks at large N.** Each particle paints `size²` pixels.
  Going from `size=1` to `size=4` is 16× the rasterizer work for the
  same particle count.
- **Calling `particles_apply_attractor` after `particles_update`.**
  The attractor still works, but its force won't see drag or
  integration until next frame, so the visual effect lags by one
  frame and feels disconnected from the attractor's motion.
- **Strong attractor + weak drag.** Spring oscillation grows
  unstable: particles overshoot, get yanked back harder, overshoot
  further. Either lower the strength or raise the drag bleed
  (`drag` further below `FP_ONE`).

---

## Reference

- API: [particles.h](../src/scenes/utils/particles.h)
- Scene: [particles.c](../src/scenes/particles.c)
- Related: [music-sync.md](music-sync.md) for FFT and sample triggers,
  [3d-graphics.md](3d-graphics.md) for the shared `Camera3D` / rotation
  convention, [2d-effects.md](2d-effects.md) for the gradient-palette
  assumption that the colour ramp builds on.
