#!/usr/bin/env python3
"""
Convert an Autodesk .3ds file to the binary model format used by the engine.

Each mesh in the .3ds is written to its own .mdl file named
<input_stem>_<mesh_name>.mdl. Lights and cameras are ignored. Missing UVs
default to (0, 0). Normals are computed per-triangle from the vertex
positions.

Binary layout (see tools/obj2model.py for the canonical spec):
    [num_triangles]
    [positions: num_triangles * 9 ints — x0,y0,z0, x1,y1,z1, x2,y2,z2 in 8.8 fp]
    [uvs:       num_triangles * 6 ints — u0,v0, u1,v1, u2,v2 in 8.8 fp]
    [normals:   num_triangles * 3 ints — nx, ny, nz in 8.8 fp]

Usage:
    python3 tools/3ds2model.py input.3ds output_dir/

No external dependencies — uses only the Python standard library.
"""

import math
import os
import re
import struct
import sys


CHUNK_MAIN      = 0x4D4D
CHUNK_EDITOR    = 0x3D3D
CHUNK_OBJECT    = 0x4000
CHUNK_TRIMESH   = 0x4100
CHUNK_VERTICES  = 0x4110
CHUNK_FACES     = 0x4120
CHUNK_TEXCOORDS = 0x4140


def iter_chunks(data, start, end):
    """Yield (id, body_start, body_end) for each chunk in [start, end)."""
    pos = start
    while pos + 6 <= end:
        cid, clen = struct.unpack_from("<HI", data, pos)
        if clen < 6 or pos + clen > end:
            return
        yield cid, pos + 6, pos + clen
        pos += clen


def parse_trimesh(data, start, end):
    vertices = []
    faces = []
    texcoords = None
    for cid, cstart, cend in iter_chunks(data, start, end):
        if cid == CHUNK_VERTICES:
            (n,) = struct.unpack_from("<H", data, cstart)
            for i in range(n):
                vertices.append(struct.unpack_from("<fff", data, cstart + 2 + i * 12))
        elif cid == CHUNK_FACES:
            (n,) = struct.unpack_from("<H", data, cstart)
            for i in range(n):
                a, b, c, _flags = struct.unpack_from("<HHHH", data, cstart + 2 + i * 8)
                faces.append((a, b, c))
        elif cid == CHUNK_TEXCOORDS:
            (n,) = struct.unpack_from("<H", data, cstart)
            texcoords = [struct.unpack_from("<ff", data, cstart + 2 + i * 8) for i in range(n)]
    return vertices, texcoords, faces


def parse_object(data, start, end):
    name_end = data.find(b"\x00", start, end)
    if name_end < 0:
        return []
    name = data[start:name_end].decode("latin-1", errors="replace")
    meshes = []
    for cid, cstart, cend in iter_chunks(data, name_end + 1, end):
        if cid == CHUNK_TRIMESH:
            verts, tcs, faces = parse_trimesh(data, cstart, cend)
            meshes.append((name, verts, tcs, faces))
    return meshes


def parse_3ds(path):
    with open(path, "rb") as f:
        data = f.read()

    meshes = []

    def walk(start, end):
        for cid, cstart, cend in iter_chunks(data, start, end):
            if cid in (CHUNK_MAIN, CHUNK_EDITOR):
                walk(cstart, cend)
            elif cid == CHUNK_OBJECT:
                meshes.extend(parse_object(data, cstart, cend))

    walk(0, len(data))
    return meshes


def compute_normal(v0, v1, v2):
    e1 = (v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2])
    e2 = (v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2])
    nx = e1[1] * e2[2] - e1[2] * e2[1]
    ny = e1[2] * e2[0] - e1[0] * e2[2]
    nz = e1[0] * e2[1] - e1[1] * e2[0]
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length < 1e-12:
        return (0.0, 0.0, 0.0)
    return (nx / length, ny / length, nz / length)


def float_to_fp8(x):
    return int(round(x * 256.0))


def sanitize(name):
    s = re.sub(r"[^A-Za-z0-9_-]", "_", name.strip())
    return s or "mesh"


def write_mdl(out_path, vertices, texcoords, faces):
    positions = []
    uvs = []
    normals = []

    for a, b, c in faces:
        v0, v1, v2 = vertices[a], vertices[b], vertices[c]
        for v in (v0, v1, v2):
            positions.append(float_to_fp8(v[0]))
            positions.append(float_to_fp8(v[1]))
            positions.append(float_to_fp8(v[2]))

        for idx in (a, b, c):
            if texcoords is not None and idx < len(texcoords):
                u, v = texcoords[idx]
            else:
                u, v = 0.0, 0.0
            uvs.append(float_to_fp8(u))
            uvs.append(float_to_fp8(v))

        n = compute_normal(v0, v1, v2)
        normals.append(float_to_fp8(n[0]))
        normals.append(float_to_fp8(n[1]))
        normals.append(float_to_fp8(n[2]))

    with open(out_path, "wb") as f:
        f.write(struct.pack("<i", len(faces)))
        for v in positions:
            f.write(struct.pack("<i", v))
        for v in uvs:
            f.write(struct.pack("<i", v))
        for v in normals:
            f.write(struct.pack("<i", v))


def convert(in_path, out_dir):
    meshes = parse_3ds(in_path)
    if not meshes:
        print(f"{in_path}: no meshes found", file=sys.stderr)
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)
    stem = sanitize(os.path.splitext(os.path.basename(in_path))[0])

    used = {}
    for name, verts, tcs, faces in meshes:
        mesh_part = sanitize(name)
        base = f"{stem}_{mesh_part}"
        n = used.get(base, 0)
        used[base] = n + 1
        filename = base if n == 0 else f"{base}_{n}"
        out_path = os.path.join(out_dir, filename + ".mdl")
        write_mdl(out_path, verts, tcs, faces)
        print(f"{in_path}[{name}] -> {out_path}: {len(faces)} triangles")


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.3ds output_dir/", file=sys.stderr)
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
