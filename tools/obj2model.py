#!/usr/bin/env python3
"""
Convert a Wavefront .obj file to the binary .mdl model format.

Faces with more than 3 vertices are fan-triangulated.

Per-vertex normals are sourced in priority order:

  1. If the .obj face uses `v/vt/vn` syntax, the referenced `vn` is
     used directly.
  2. Otherwise faces are grouped by their `s` (smoothing-group)
     directive and per-vertex normals are computed as the unit-length
     sum of face normals over all faces that share both the geometric
     position and the smoothing group. `s 0` / `s off` faces are not
     averaged with anything: each such vertex stores the face normal
     of its own face.

Binary layout (little-endian 32-bit signed integers):
    [num_triangles]
    [positions:      num_triangles * 9 ints  in 8.8 fp]
    [uvs:            num_triangles * 6 ints  in 8.8 fp]
    [face_normals:   num_triangles * 3 ints  in 8.8 fp]
    [vertex_normals: num_triangles * 9 ints  in 8.8 fp]

Usage:
    python3 tools/obj2model.py input.obj output.mdl

No external dependencies.
"""

import math
import struct
import sys


def parse_obj(path):
    """Parse an .obj file. Returns (vertices, texcoords, normals, faces).

    Each face is a tuple (smooth_group, [(vi, ti, ni), ...]) where indices
    are 0-based; ti and ni are -1 if not specified."""
    vertices = []
    texcoords = []
    normals = []
    faces = []
    smooth = 0  # 0 = off

    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            key = parts[0]

            if key == "v" and len(parts) >= 4:
                vertices.append((float(parts[1]), float(parts[2]),
                                 float(parts[3])))
            elif key == "vt" and len(parts) >= 3:
                texcoords.append((float(parts[1]), float(parts[2])))
            elif key == "vn" and len(parts) >= 4:
                normals.append((float(parts[1]), float(parts[2]),
                                float(parts[3])))
            elif key == "s":
                arg = parts[1] if len(parts) > 1 else "0"
                if arg in ("off", "0"):
                    smooth = 0
                else:
                    try:
                        smooth = int(arg)
                    except ValueError:
                        smooth = 0
            elif key == "f":
                face_verts = []
                for p in parts[1:]:
                    indices = p.split("/")
                    vi = int(indices[0]) - 1
                    ti = (int(indices[1]) - 1
                          if len(indices) > 1 and indices[1] else -1)
                    ni = (int(indices[2]) - 1
                          if len(indices) > 2 and indices[2] else -1)
                    face_verts.append((vi, ti, ni))
                faces.append((smooth, face_verts))

    return vertices, texcoords, normals, faces


def triangulate(faces):
    """Fan-triangulate. Returns list of (smooth, [(vi,ti,ni)]*3)."""
    triangles = []
    for smooth, face in faces:
        for i in range(1, len(face) - 1):
            triangles.append((smooth, [face[0], face[i], face[i + 1]]))
    return triangles


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


def compute_vertex_normals(vertices, triangles, face_normals):
    """For each (triangle, vertex_in_triangle) pair, compute the vertex
    normal: sum of face normals of all triangles sharing this position
    in the same smoothing group, then normalised. `s 0` triangles get
    their own face normal back (no averaging)."""

    # Bucket: (vertex_index, smooth_group) -> list of triangle indices.
    # smooth=0 buckets only contain that one triangle (we use a unique
    # sentinel so they never average together).
    buckets = {}
    for ti, (smooth, verts) in enumerate(triangles):
        for vi, _, _ in verts:
            if smooth == 0:
                key = (vi, ("flat", ti))
            else:
                key = (vi, smooth)
            buckets.setdefault(key, []).append(ti)

    out = []
    for ti, (smooth, verts) in enumerate(triangles):
        for vi, _, _ in verts:
            if smooth == 0:
                key = (vi, ("flat", ti))
            else:
                key = (vi, smooth)
            members = buckets[key]
            if len(members) == 1:
                out.append(face_normals[members[0]])
            else:
                ax = ay = az = 0.0
                for m in members:
                    n = face_normals[m]
                    ax += n[0]
                    ay += n[1]
                    az += n[2]
                out.append(normalize((ax, ay, az)))
    return out


def f88(x):
    return int(round(x * 256.0))


def convert(obj_path, out_path):
    vertices, texcoords, file_normals, faces = parse_obj(obj_path)
    triangles = triangulate(faces)
    num_triangles = len(triangles)

    # Compute the face normal for every triangle (used both for the
    # face_normals output array and as the basis for averaged vertex
    # normals when the file doesn't supply per-vertex normals).
    face_normals_geom = []
    for _, verts in triangles:
        v0 = vertices[verts[0][0]]
        v1 = vertices[verts[1][0]]
        v2 = vertices[verts[2][0]]
        face_normals_geom.append(face_normal(v0, v1, v2))

    # Vertex normals from file-supplied `vn` if every vertex of every
    # triangle references one; otherwise computed from smoothing groups.
    have_file_vns = all(
        all(vn_idx >= 0 and vn_idx < len(file_normals) for _, _, vn_idx in v)
        for _, v in triangles)
    if have_file_vns:
        vertex_normals = []
        for _, verts in triangles:
            for _, _, ni in verts:
                vertex_normals.append(file_normals[ni])
    else:
        vertex_normals = compute_vertex_normals(vertices, triangles,
                                                face_normals_geom)

    positions = []
    uvs = []
    face_normals_out = []
    vertex_normals_out = []

    for ti, (_, verts) in enumerate(triangles):
        for vi, _, _ in verts:
            v = vertices[vi]
            positions.extend([f88(v[0]), f88(v[1]), f88(v[2])])

        for _, ti_idx, _ in verts:
            if 0 <= ti_idx < len(texcoords):
                uv = texcoords[ti_idx]
            else:
                uv = (0.0, 0.0)
            uvs.extend([f88(uv[0]), f88(uv[1])])

        n = face_normals_geom[ti]
        face_normals_out.extend([f88(n[0]), f88(n[1]), f88(n[2])])

    for n in vertex_normals:
        vertex_normals_out.extend([f88(n[0]), f88(n[1]), f88(n[2])])

    with open(out_path, "wb") as f:
        f.write(struct.pack("<i", num_triangles))
        for v in positions:
            f.write(struct.pack("<i", v))
        for v in uvs:
            f.write(struct.pack("<i", v))
        for v in face_normals_out:
            f.write(struct.pack("<i", v))
        for v in vertex_normals_out:
            f.write(struct.pack("<i", v))

    src = "file vns" if have_file_vns else "smoothing groups"
    print("%s -> %s: %d triangles (%s)"
          % (obj_path, out_path, num_triangles, src))


def main():
    if len(sys.argv) != 3:
        print("Usage: %s input.obj output.mdl" % sys.argv[0], file=sys.stderr)
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
