#!/usr/bin/env python3
"""
Convert a Wavefront .obj file to the binary model format used by the engine.

Binary layout (all values are little-endian 32-bit signed integers):
    [num_triangles]
    [positions: num_triangles * 9 ints — x0,y0,z0, x1,y1,z1, x2,y2,z2 in 8.8 fp]
    [uvs:       num_triangles * 6 ints — u0,v0, u1,v1, u2,v2 in 8.8 fp]
    [normals:   num_triangles * 3 ints — nx, ny, nz in 8.8 fp]

Faces with more than 3 vertices are triangulated as a fan.
Missing UVs default to (0, 0).

Usage:
    python3 tools/obj2model.py input.obj output.mdl

No external dependencies — uses only the Python standard library.
"""

import struct
import sys
import math


def parse_obj(path):
    """Parse an .obj file, returning lists of vertices, texcoords, and faces."""
    vertices = []
    texcoords = []
    faces = []

    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            key = parts[0]

            if key == "v" and len(parts) >= 4:
                vertices.append(
                    (float(parts[1]), float(parts[2]), float(parts[3]))
                )
            elif key == "vt" and len(parts) >= 3:
                texcoords.append((float(parts[1]), float(parts[2])))
            elif key == "f":
                face_verts = []
                for p in parts[1:]:
                    indices = p.split("/")
                    vi = int(indices[0]) - 1
                    ti = int(indices[1]) - 1 if len(indices) > 1 and indices[1] else -1
                    face_verts.append((vi, ti))
                faces.append(face_verts)

    return vertices, texcoords, faces


def triangulate(faces):
    """Fan-triangulate faces with more than 3 vertices."""
    triangles = []
    for face in faces:
        for i in range(1, len(face) - 1):
            triangles.append((face[0], face[i], face[i + 1]))
    return triangles


def compute_normal(v0, v1, v2):
    """Compute face normal from three vertices. Returns unit normal."""
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
    """Convert float to 8.8 fixed-point integer."""
    return int(round(x * 256.0))


def convert(obj_path, out_path):
    vertices, texcoords, faces = parse_obj(obj_path)
    triangles = triangulate(faces)
    num_triangles = len(triangles)

    positions = []
    uvs = []
    normals = []

    for tri in triangles:
        verts = []
        for vi, ti in tri:
            v = vertices[vi]
            verts.append(v)
            positions.append(float_to_fp8(v[0]))
            positions.append(float_to_fp8(v[1]))
            positions.append(float_to_fp8(v[2]))

        for vi, ti in tri:
            if ti >= 0 and ti < len(texcoords):
                uv = texcoords[ti]
            else:
                uv = (0.0, 0.0)
            uvs.append(float_to_fp8(uv[0]))
            uvs.append(float_to_fp8(uv[1]))

        n = compute_normal(verts[0], verts[1], verts[2])
        normals.append(float_to_fp8(n[0]))
        normals.append(float_to_fp8(n[1]))
        normals.append(float_to_fp8(n[2]))

    with open(out_path, "wb") as f:
        f.write(struct.pack("<i", num_triangles))
        for v in positions:
            f.write(struct.pack("<i", v))
        for v in uvs:
            f.write(struct.pack("<i", v))
        for v in normals:
            f.write(struct.pack("<i", v))

    print(f"{obj_path} -> {out_path}: {num_triangles} triangles")


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.obj output.mdl", file=sys.stderr)
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
