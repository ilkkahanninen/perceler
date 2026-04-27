# 3D graphics

A walkthrough of the 3D pipeline aimed at someone writing a new scene. API
reference for each function lives in the corresponding header — this guide
is about *which* function to pick and *why*.

The pipeline:

```
mesh source ─► transform ─► project ─► (per-vertex shade) ─► rasterize
              (rotate +    (perspective    (lambert,
               translate)   to screen)      sphere-map)
```

Every coordinate is Q8.8 fixed-point (see [math.h](../src/scenes/utils/math.h#L25-L46)).
Angles are 8-bit (`0..255 == 0..2π`). Models are vertex-indexed: a triangle
is three indices into shared `positions[]`, `uvs[]`, `vertex_normals[]`
arrays, plus its own `face_normals` entry. Per-frame work happens once per
unique vertex, then the triangle loop just looks up by index.

---

## Step 1 — get a mesh

Three options:

| Source                                | When to use                                                                |
| ------------------------------------- | -------------------------------------------------------------------------- |
| `polyhedron_create()`                 | Platonic solids (tetra/cube/octa/icosa), optionally extruded into prisms.  |
| `tube_create()` / `rope_create()`     | Sweep a polyline into a smooth tube or grooved rope.                       |
| `model_load(ASSET_FOO_MDL, flags)`    | Pre-authored mesh from `.obj` (auto-converted) or `.3ds` (manual convert). |

All three return a `Model *` with the same shape ([model.h](../src/scenes/utils/model.h)).

For `model_load`, pass a flags combination so only the buffers you need are
allocated:

| Flag combo        | Loads                                | Use for             |
| ----------------- | ------------------------------------ | ------------------- |
| `MODEL_WIREFRAME` | positions + face_normals             | wireframe + cull    |
| `MODEL_FLAT`      | positions + face_normals             | flat shading        |
| `MODEL_GOURAUD`   | + vertex_normals                     | per-vertex lighting |
| `MODEL_TEXTURED`  | positions + uvs + face_normals       | textured w/o lights |
| `MODEL_ALL`       | everything                           | textured + Gouraud  |

`indices` is always loaded.

---

## Step 2 — pick a rasterizer

Five choices in [render3d.h](../src/scenes/utils/render3d.h):

| Function                             | Per-pixel cost                    | When to use                                                |
| ------------------------------------ | --------------------------------- | ---------------------------------------------------------- |
| `draw_line()` (from `draw.h`)        | 1 store                           | Wireframe.                                                 |
| `fill_triangle_flat`                 | 1 store + z-test                  | Flat colour per triangle. Cheapest fill.                   |
| `fill_triangle_gouraud`              | 1 add + 1 store + z-test          | Smooth shading, no texture.                                |
| `fill_triangle_textured_affine`      | 2 adds + 1 lookup + z-test        | Textured, UVs vary slowly *or* are sphere-mapped.          |
| `fill_triangle_textured`             | + perspective divide every 16 px  | Textured, UVs vary fast (large near triangles).            |
| `fill_triangle_textured_gouraud`     | textured + colormap lookup        | Textured + per-vertex lighting.                            |

**Affine vs perspective-correct** is the most common decision. Affine UV
interpolation is faster and exact for constant-UV inputs. The classic
"texture swimming" only shows up on big triangles where the UVs vary across
the screen-foreshortened axis. For sphere-mapping (constant UVs across each
triangle's vertices) **always pick affine** — perspective rounding actually
introduces drift.

---

## Step 3 — smooth or faceted?

Vertex normals decide whether neighbouring triangles share lighting/UV
across their shared edge.

| You want                  | Mesh source                                 | Flag                  |
| ------------------------- | ------------------------------------------- | --------------------- |
| Faceted (hard edges)      | `polyhedron_create(..., 0)`                 | flags = 0             |
| Smooth (rounded)          | `polyhedron_create(..., POLYHEDRON_SMOOTH)` | `POLYHEDRON_SMOOTH`   |
| Smooth tube/rope          | `tube_create(..., POLYHEDRON_SMOOTH)`       | `POLYHEDRON_SMOOTH`   |
| .obj / .3ds               | exporter honours `s` smoothing groups       | (in source asset)     |

Smooth mode for `polyhedron_create()` merges vertices by position and
averages adjacent face normals. **Side effect**: per-face UVs collapse to
one UV per shared vertex — fine for sphere-mapping (which doesn't use
UVs), wrong if you need per-face UV islands. If you need both, author a
`.obj` instead.

---

## Step 4 — set up the scene

Standard `setup()` shape for a textured + Gouraud scene:

```c
static Model *mesh;
static Texture *texture;
static Colormap *colormap;
static int *transformed;        /* num_vertices * 3 */
static int *transformed_fnorms; /* num_triangles * 3 */
static int *transformed_vnorms; /* num_vertices * 3 */
static int *vertex_lambert;     /* num_vertices */
static int *screen_xy;          /* num_vertices * 2 */
static signed char *visible;    /* num_vertices */
static unsigned short *zbuffer;
static int num_tris, num_verts;

static void setup(void)
{
    mesh = polyhedron_create(POLYHEDRON_CUBE, 0, 0, 0);
    texture = texture_load(ASSET_MARBLE_BMP);
    colormap = malloc(sizeof(Colormap));
    colormap_build(colormap, &texture->palette);

    num_tris  = mesh->num_triangles;
    num_verts = mesh->num_vertices;

    transformed        = malloc(num_verts * 3 * sizeof(int));
    transformed_fnorms = malloc(num_tris  * 3 * sizeof(int));
    transformed_vnorms = malloc(num_verts * 3 * sizeof(int));
    vertex_lambert     = malloc(num_verts * sizeof(int));
    screen_xy          = malloc(num_verts * 2 * sizeof(int));
    visible            = malloc(num_verts);
    zbuffer            = malloc(VGA_SIZE * sizeof(unsigned short));
}
```

Drop the buffers you don't need (e.g. wireframe doesn't need `zbuffer` or
`vnorms`; flat shading doesn't need `vnorms` or `vertex_lambert`).

Pair every `malloc` with a `free` in `shutdown()`. If the texture has its
own palette, apply it in `init()` (palette state is not persistent across
scene transitions).

`init()` runs every time the scene becomes active; `setup()` runs once at
boot. Put one-shot allocation in `setup()`, palette/state reset in
`init()`.

---

## Step 5 — the render loop

The canonical pattern:

```c
static void render(const RenderContext *ctx)
{
    unsigned char *backbuffer = ctx->backbuffer;
    unsigned char ay = (unsigned char)ctx->frame;       /* rotation Y */
    unsigned char ax = (unsigned char)(ctx->frame >> 1);/* rotation X */
    const int *indices = mesh->indices;
    const int *uvs = mesh->uvs;
    int i;

    memset(backbuffer, 0, VGA_SIZE);

    /* (1) Transform unique vertices once each. */
    transform_points(transformed, mesh->positions, num_verts, ay, ax,
                     camera.cam_z);
    transform_dirs(transformed_fnorms, mesh->face_normals, num_tris, ay, ax);
    transform_dirs(transformed_vnorms, mesh->vertex_normals, num_verts,
                   ay, ax);

    /* (2) Project all vertices once. */
    project_points(&camera, transformed, num_verts, screen_xy, visible);

    /* (3) Per-vertex shading once. */
    for (i = 0; i < num_verts; i++)
        vertex_lambert[i] = lambert(transformed_vnorms + i * 3);

    memset(zbuffer, 0, VGA_SIZE * sizeof(unsigned short));

    /* (4) Per-triangle: cull, look up, draw. */
    for (i = 0; i < num_tris; i++)
    {
        int i0 = indices[i*3+0], i1 = indices[i*3+1], i2 = indices[i*3+2];
        int *v0 = transformed + i0 * 3;
        int *fn = transformed_fnorms + i * 3;

        if (backface3d(fn, v0)) continue;
        if (!visible[i0] || !visible[i1] || !visible[i2]) continue;

        fill_triangle_textured_gouraud(backbuffer, zbuffer,
            screen_xy[i0*2], screen_xy[i0*2+1], v0[2],
            uvs[i0*2], uvs[i0*2+1], vertex_lambert[i0],
            ... /* same for i1, i2 */,
            texture, colormap);
    }

    vga_vsync();
    vga_blit(backbuffer);
}
```

Key invariants:

- The z passed to rasterizers is the **transformed view-space z** (Q8.8),
  not screen-space — they reconstruct 1/z internally.
- Backface test (`backface3d`) wants the transformed face normal *and any
  one transformed vertex* of the triangle. Use `v0`.
- `visible[]` must be checked for all three vertices; a triangle straddling
  the near plane should be skipped (no near-plane clipping is done — the
  whole triangle is dropped).
- Z-buffer values are 1/z, so clear to 0 means "infinitely far": the first
  pixel always wins.

---

## Sphere mapping

Drop-in environment mapping for smooth surfaces. Computes `(u, v)` from a
camera-space normal:

```c
sphere_map_uv(transformed_vnorms[i*3+0], transformed_vnorms[i*3+1],
              &vertex_uv[i*2+0], &vertex_uv[i*2+1]);
```

Use it when:

- The model has smooth (averaged) vertex normals.
- You don't care about the original UVs (sphere-map *replaces* them).
- You want a "shiny chrome" / environment look.

Always pair with `fill_triangle_textured_affine` — sphere-map UVs are
constant across each triangle, so the perspective-correct path would just
introduce rounding drift.

Reference scenes: [spheremap_orb.c](../src/scenes/spheremap_orb.c),
[rope_knot.c](../src/scenes/rope_knot.c).

---

## Camera tuning

```c
static const Camera3D camera = {
    INT_TO_FP(4),    /* cam_z       — distance from origin */
    FP_ONE >> 2,     /* near_z      — 0.25, conservative */
    INT_TO_FP(220),  /* proj_scale  — focal length */
    160, 100         /* cx, cy      — screen centre */
};
```

- **`cam_z`**: larger → mesh appears smaller. Pick to match the mesh's
  bounding sphere (polyhedron meshes are normalised to radius 1; a scale
  factor would multiply that). Cube scenes use 4, teapot 6, sphere-mapped
  orb 3.
- **`proj_scale`**: focal length in pixels. The example scenes use
  180–220 for 320×200; lower → wider angle.
- **`near_z`**: anything closer is rejected. Keep it small enough that
  rotating geometry never *fully* enters the frustum behind it (otherwise
  you'll see triangles vanish), but large enough that 1/z doesn't blow up.
  `FP_ONE >> 2` (= 0.25) is the standard.

There is no far plane — z-buffer wraparound is the only constraint, and
the iz reciprocal already handles it.

---

## Performance

Where time goes per frame, ordered roughly:

1. **Rasterizer fill**, scaling with total covered triangle area. The
   per-pixel cost ranking from the inner loops in [render3d.c](../src/scenes/utils/render3d.c)
   is flat ≤ Gouraud ≤ affine textured ≤ textured + Gouraud ≤
   perspective-correct textured (which adds a divide every 16 pixels —
   `TEX_SUBDIV` in render3d.c).
2. **Per-vertex transforms**, one rotation each. Linear in
   `num_vertices`, not `num_triangles*3` — the indexed mesh saves you
   here.
3. **Backface test + index lookups**, linear in `num_triangles`.

If you're CPU-bound:

- Reduce poly count first.
- Prefer `fill_triangle_textured_affine` when the geometry permits.
- Use `MODEL_FLAT` rather than `MODEL_GOURAUD` when the lighting doesn't
  need to change across a triangle.
- Don't transform vertex normals if you only need flat shading.
- Render at half resolution (160×100) and pixel-double via
  `vga_blit_2x_to_buffer`. Quarters the per-pixel work at the cost of
  chunkier edges. See [new-scene.md](new-scene.md#half-resolution-rendering).
- Render only every other scanline per frame (interleaving) and use
  `vga_blit_rows` to push the touched lines. Halves the per-pixel
  work; each line refreshes every other frame, so motion gets a comb
  artifact. See [new-scene.md](new-scene.md#interleaved-rendering).

Memory:

- The colormap is 16 KB (`Colormap.map[64 * 256]`).
- The z-buffer is `VGA_SIZE * sizeof(unsigned short)` = 128000 bytes.
  Skip it for non-occluding scenes (wireframe, single convex hull where
  backface culling already covers it).

---

## Common pitfalls

- **Forgetting `init()` resets palette.** A scene with its own texture
  palette must call `palette_apply(&texture->palette)` in `init()` (not
  just `setup()`), otherwise the previous scene's palette persists when
  you scrub back via Left arrow.
- **Reusing `MODEL_FLAT` flags with a Gouraud rasterizer.** `vertex_normals`
  will be NULL; you'll segfault on the first triangle. Match the flag set
  to what your `render()` actually reads.
- **Passing screen-space z to the rasterizer.** It needs view-space z so
  it can compute 1/z; the rasterizer's "z0" parameter is `transformed[i*3+2]`,
  not `screen_xy[..]`. (There is no screen-space z in this engine.)
- **Sphere-map mode without smooth normals.** Faceted normals make the
  texture jump per-triangle. Set `POLYHEDRON_SMOOTH` (or use a smoothed
  source `.obj`).
- **Calling rasterizers when one vertex is behind the camera.** No clipping
  happens — `project_points` will set `visible[i] = 0` and the loop *must*
  skip the triangle (`!visible[i0] || !visible[i1] || !visible[i2]`).
  Forgetting that check produces wraparound triangles flying across the
  screen.
- **Mismatched `cam_z` and `near_z`.** If `near_z >= cam_z`, the mesh is
  always behind the near plane on the closest face and disappears. Keep
  `near_z` small.

---

## Reference scenes

| Scene                                                  | Demonstrates                                 |
| ------------------------------------------------------ | -------------------------------------------- |
| [polyhedra.c](../src/scenes/polyhedra.c)               | Wireframe, no z-buffer, gallery cycling      |
| [model_wireframe.c](../src/scenes/model_wireframe.c)   | Wireframe with backface cull                 |
| [model_flatshade.c](../src/scenes/model_flatshade.c)   | Flat shading + per-face Lambert              |
| [model_gouraud.c](../src/scenes/model_gouraud.c)       | Per-vertex Gouraud                           |
| [textured_cube.c](../src/scenes/textured_cube.c)       | Perspective-correct texture                  |
| [shaded_cube.c](../src/scenes/shaded_cube.c)           | Textured + Gouraud via colormap              |
| [spheremap_orb.c](../src/scenes/spheremap_orb.c)       | Sphere-mapped icosahedron                    |
| [rope_knot.c](../src/scenes/rope_knot.c)               | Procedural rope along a knotted path         |
