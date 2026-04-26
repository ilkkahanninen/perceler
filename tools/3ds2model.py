#!/usr/bin/env python3
"""
Convert an Autodesk .3ds file to the binary .mdl model format.

Each mesh in the .3ds is written to its own .mdl file named
<input_stem>_<mesh_name>.mdl. Lights and cameras are ignored.

Face normals are computed per-triangle from the vertex positions.
Per-vertex normals are derived from face smoothing-group bitmasks
(chunk 0x4150): two faces smooth across a shared edge if their masks
share at least one bit; faces with mask 0 are kept hard. Missing UVs
default to (0, 0).

Binary layout (see model.h for the canonical spec):
    [num_triangles]
    [positions:      num_triangles * 9 ints  in 8.8 fp]
    [uvs:            num_triangles * 6 ints  in 8.8 fp]
    [face_normals:   num_triangles * 3 ints  in 8.8 fp]
    [vertex_normals: num_triangles * 9 ints  in 8.8 fp]

Usage:
    python3 tools/3ds2model.py input.3ds output_dir/

No external dependencies.
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
CHUNK_SMOOTHING = 0x4150


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
    smoothing = None
    texcoords = None
    faces_start = faces_end = None

    for cid, cstart, cend in iter_chunks(data, start, end):
        if cid == CHUNK_VERTICES:
            (n,) = struct.unpack_from("<H", data, cstart)
            for i in range(n):
                vertices.append(struct.unpack_from("<fff", data,
                                                   cstart + 2 + i * 12))
        elif cid == CHUNK_FACES:
            (n,) = struct.unpack_from("<H", data, cstart)
            for i in range(n):
                a, b, c, _flags = struct.unpack_from("<HHHH", data,
                                                    cstart + 2 + i * 8)
                faces.append((a, b, c))
            # Sub-chunks (smoothing, etc.) live after the face data.
            faces_start = cstart + 2 + n * 8
            faces_end = cend
        elif cid == CHUNK_TEXCOORDS:
            (n,) = struct.unpack_from("<H", data, cstart)
            texcoords = [struct.unpack_from("<ff", data, cstart + 2 + i * 8)
                         for i in range(n)]

    if faces_start is not None:
        for cid, cstart, cend in iter_chunks(data, faces_start, faces_end):
            if cid == CHUNK_SMOOTHING:
                smoothing = []
                # One u32 bitmask per face, in face order.
                for i in range(len(faces)):
                    (m,) = struct.unpack_from("<I", data, cstart + i * 4)
                    smoothing.append(m)

    if smoothing is None:
        smoothing = [0] * len(faces)

    return vertices, texcoords, faces, smoothing


def parse_object(data, start, end):
    name_end = data.find(b"\x00", start, end)
    if name_end < 0:
        return []
    name = data[start:name_end].decode("latin-1", errors="replace")
    meshes = []
    for cid, cstart, cend in iter_chunks(data, name_end + 1, end):
        if cid == CHUNK_TRIMESH:
            verts, tcs, faces, smoothing = parse_trimesh(data, cstart, cend)
            meshes.append((name, verts, tcs, faces, smoothing))
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


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def normalize(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if n < 1e-12:
        return (0.0, 0.0, 0.0)
    return (v[0] / n, v[1] / n, v[2] / n)


def face_normal(v0, v1, v2):
    return normalize(cross(sub(v1, v0), sub(v2, v0)))


def compute_vertex_normals(faces, smoothing, face_normals):
    """For each face vertex, sum face normals across faces that
    share the position AND share at least one smoothing bit, then
    normalise. Faces with mask 0 keep their face normal verbatim."""

    # Index: vertex_position -> list of (face_index, mask)
    position_to_faces = {}
    for fi, (a, b, c) in enumerate(faces):
        m = smoothing[fi]
        for vi in (a, b, c):
            position_to_faces.setdefault(vi, []).append((fi, m))

    out = []
    for fi, (a, b, c) in enumerate(faces):
        my_mask = smoothing[fi]
        my_n = face_normals[fi]
        for vi in (a, b, c):
            if my_mask == 0:
                out.append(my_n)
                continue
            ax = ay = az = 0.0
            for ofi, om in position_to_faces[vi]:
                if om & my_mask:
                    n = face_normals[ofi]
                    ax += n[0]
                    ay += n[1]
                    az += n[2]
            out.append(normalize((ax, ay, az)))
    return out


def f88(x):
    return int(round(x * 256.0))


def sanitize(name):
    s = re.sub(r"[^A-Za-z0-9_-]", "_", name.strip())
    return s or "mesh"


def write_mdl(out_path, vertices, texcoords, faces, smoothing):
    face_normals = []
    for a, b, c in faces:
        face_normals.append(face_normal(vertices[a], vertices[b], vertices[c]))

    vertex_normals = compute_vertex_normals(faces, smoothing, face_normals)

    positions = []
    uvs = []
    fn_out = []
    vn_out = []

    for fi, (a, b, c) in enumerate(faces):
        v0, v1, v2 = vertices[a], vertices[b], vertices[c]
        for v in (v0, v1, v2):
            positions.extend([f88(v[0]), f88(v[1]), f88(v[2])])

        for idx in (a, b, c):
            if texcoords is not None and idx < len(texcoords):
                u, v = texcoords[idx]
            else:
                u, v = 0.0, 0.0
            uvs.extend([f88(u), f88(v)])

        n = face_normals[fi]
        fn_out.extend([f88(n[0]), f88(n[1]), f88(n[2])])

    for n in vertex_normals:
        vn_out.extend([f88(n[0]), f88(n[1]), f88(n[2])])

    with open(out_path, "wb") as f:
        f.write(struct.pack("<i", len(faces)))
        for v in positions:
            f.write(struct.pack("<i", v))
        for v in uvs:
            f.write(struct.pack("<i", v))
        for v in fn_out:
            f.write(struct.pack("<i", v))
        for v in vn_out:
            f.write(struct.pack("<i", v))


def convert(in_path, out_dir):
    meshes = parse_3ds(in_path)
    if not meshes:
        print("%s: no meshes found" % in_path, file=sys.stderr)
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)
    stem = sanitize(os.path.splitext(os.path.basename(in_path))[0])

    used = {}
    for name, verts, tcs, faces, smoothing in meshes:
        mesh_part = sanitize(name)
        base = "%s_%s" % (stem, mesh_part)
        n = used.get(base, 0)
        used[base] = n + 1
        filename = base if n == 0 else "%s_%d" % (base, n)
        out_path = os.path.join(out_dir, filename + ".mdl")
        write_mdl(out_path, verts, tcs, faces, smoothing)
        print("%s[%s] -> %s: %d triangles"
              % (in_path, name, out_path, len(faces)))


def main():
    if len(sys.argv) != 3:
        print("Usage: %s input.3ds output_dir/" % sys.argv[0],
              file=sys.stderr)
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
