#!/usr/bin/env python3
"""
Convert a PNG image to a 256-color indexed BMP using a reference palette.

Reads the palette from an existing 8-bit indexed BMP file and maps each
pixel in the source PNG to the closest palette entry.  Transparent pixels
(alpha below the threshold) are mapped to index 0.

Usage:
    python3 tools/png2bmp.py input.png output.bmp [options]

Options:
    -p, --palette FILE    Reference palette BMP (default: palette.bmp
                          next to this script, or build/palette.bmp)
    -t, --threshold INT   Alpha threshold 0-255 (default: 128).
                          Pixels with alpha < threshold become index 0.

No external dependencies — uses only the Python standard library.
"""

import argparse
import os
import struct
import sys
import zlib


# ======================================================================
# PNG reader (RGBA output, stdlib only)
# ======================================================================

def _read_png_chunks(data):
    """Yield (chunk_type, chunk_data) from raw PNG bytes."""
    pos = 8  # skip signature
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        ctype = data[pos+4:pos+8]
        cdata = data[pos+8:pos+8+length]
        pos += 12 + length  # 4 len + 4 type + data + 4 crc
        yield ctype, cdata


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    elif pb <= pc:
        return b
    return c


def _unfilter(raw, width, height, bpp):
    """Reconstruct scanlines from filtered PNG data."""
    stride = width * bpp
    out = bytearray(height * stride)
    pos = 0
    prev_row = bytearray(stride)
    for y in range(height):
        ftype = raw[pos]; pos += 1
        row = bytearray(stride)
        for x in range(stride):
            cur = raw[pos]; pos += 1
            a = row[x - bpp] if x >= bpp else 0
            b = prev_row[x]
            c = prev_row[x - bpp] if x >= bpp else 0
            if ftype == 0:
                row[x] = cur & 0xFF
            elif ftype == 1:
                row[x] = (cur + a) & 0xFF
            elif ftype == 2:
                row[x] = (cur + b) & 0xFF
            elif ftype == 3:
                row[x] = (cur + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                row[x] = (cur + _paeth(a, b, c)) & 0xFF
        out[y * stride:(y + 1) * stride] = row
        prev_row = row
    return bytes(out)


def read_png_rgba(path):
    """Read a PNG file, return (width, height, pixels) where pixels is
    a list of (R, G, B, A) tuples in row-major order."""
    with open(path, 'rb') as f:
        data = f.read()

    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path} is not a valid PNG file')

    ihdr = plte = trns = None
    idat_parts = []

    for ctype, cdata in _read_png_chunks(data):
        if ctype == b'IHDR':
            ihdr = cdata
        elif ctype == b'PLTE':
            plte = cdata
        elif ctype == b'tRNS':
            trns = cdata
        elif ctype == b'IDAT':
            idat_parts.append(cdata)

    width, height, bit_depth, color_type = struct.unpack('>IIBB', ihdr[:10])
    if bit_depth != 8:
        raise ValueError(f'Only 8-bit PNGs are supported (got {bit_depth})')

    raw = zlib.decompress(b''.join(idat_parts))

    if color_type == 6:  # RGBA
        pixel_data = _unfilter(raw, width, height, 4)
        pixels = []
        for i in range(0, len(pixel_data), 4):
            pixels.append((pixel_data[i], pixel_data[i+1],
                           pixel_data[i+2], pixel_data[i+3]))
    elif color_type == 2:  # RGB (no alpha)
        pixel_data = _unfilter(raw, width, height, 3)
        # Check for tRNS transparent color
        tr = tg = tb = None
        if trns and len(trns) >= 6:
            tr = struct.unpack('>H', trns[0:2])[0]
            tg = struct.unpack('>H', trns[2:4])[0]
            tb = struct.unpack('>H', trns[4:6])[0]
        pixels = []
        for i in range(0, len(pixel_data), 3):
            r, g, b = pixel_data[i], pixel_data[i+1], pixel_data[i+2]
            a = 0 if (tr is not None and r == tr and g == tg and b == tb) else 255
            pixels.append((r, g, b, a))
    elif color_type == 3:  # Indexed
        if plte is None:
            raise ValueError('Indexed PNG without PLTE chunk')
        pal = []
        for i in range(0, len(plte), 3):
            pal.append((plte[i], plte[i+1], plte[i+2]))
        # tRNS for indexed gives per-entry alpha
        pal_alpha = list(trns) if trns else []
        pixel_data = _unfilter(raw, width, height, 1)
        pixels = []
        for b in pixel_data:
            r, g, bl = pal[b]
            a = pal_alpha[b] if b < len(pal_alpha) else 255
            pixels.append((r, g, bl, a))
    elif color_type == 4:  # Greyscale + alpha
        pixel_data = _unfilter(raw, width, height, 2)
        pixels = []
        for i in range(0, len(pixel_data), 2):
            v, a = pixel_data[i], pixel_data[i+1]
            pixels.append((v, v, v, a))
    elif color_type == 0:  # Greyscale
        pixel_data = _unfilter(raw, width, height, 1)
        tr = None
        if trns and len(trns) >= 2:
            tr = struct.unpack('>H', trns[0:2])[0]
        pixels = []
        for b in pixel_data:
            a = 0 if (tr is not None and b == tr) else 255
            pixels.append((b, b, b, a))
    else:
        raise ValueError(f'Unsupported PNG color type {color_type}')

    return width, height, pixels


# ======================================================================
# BMP palette reader
# ======================================================================

def read_bmp_palette(path):
    """Read palette from an 8-bit indexed BMP. Returns list of 256 (R,G,B)."""
    with open(path, 'rb') as f:
        data = f.read()

    if data[:2] != b'BM':
        raise ValueError(f'{path} is not a valid BMP file')

    info_size = struct.unpack_from('<I', data, 14)[0]
    bits = struct.unpack_from('<H', data, 28)[0]
    if bits != 8:
        raise ValueError(f'Palette BMP must be 8-bit indexed (got {bits}-bit)')

    colors_used = struct.unpack_from('<I', data, 46)[0]
    if colors_used == 0:
        colors_used = 256

    table_offset = 14 + info_size
    pal = []
    for i in range(colors_used):
        off = table_offset + i * 4
        b, g, r = data[off], data[off+1], data[off+2]
        pal.append((r, g, b))

    # Pad to 256 if needed
    while len(pal) < 256:
        pal.append((0, 0, 0))

    return pal


# ======================================================================
# Color matching
# ======================================================================

def find_closest(r, g, b, palette, start=0):
    """Find the palette index closest to (r, g, b) by squared distance.
    Search starts at 'start' to allow skipping index 0 (reserved for
    transparency)."""
    best_idx = start
    best_dist = float('inf')
    for i in range(start, len(palette)):
        pr, pg, pb = palette[i]
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_dist:
            best_dist = d
            best_idx = i
            if d == 0:
                break
    return best_idx


# ======================================================================
# BMP writer
# ======================================================================

def write_indexed_bmp(path, width, height, palette, indices):
    """Write an 8-bit indexed BMP."""
    stride = (width + 3) & ~3
    pixel_data_size = stride * height
    color_table_size = 256 * 4
    header_size = 14 + 40
    pixel_offset = header_size + color_table_size
    file_size = pixel_offset + pixel_data_size

    with open(path, 'wb') as f:
        # File header
        f.write(b'BM')
        f.write(struct.pack('<I', file_size))
        f.write(struct.pack('<HH', 0, 0))
        f.write(struct.pack('<I', pixel_offset))

        # Info header
        f.write(struct.pack('<I', 40))
        f.write(struct.pack('<i', width))
        f.write(struct.pack('<i', height))
        f.write(struct.pack('<HH', 1, 8))
        f.write(struct.pack('<I', 0))
        f.write(struct.pack('<I', pixel_data_size))
        f.write(struct.pack('<i', 0))
        f.write(struct.pack('<i', 0))
        f.write(struct.pack('<I', 256))
        f.write(struct.pack('<I', 0))

        # Color table (BGRA)
        for r, g, b in palette:
            f.write(struct.pack('BBBB', b, g, r, 0))

        # Pixel data (bottom-to-top)
        pad = b'\x00' * (stride - width)
        for row in range(height - 1, -1, -1):
            offset = row * width
            f.write(bytes(indices[offset:offset + width]))
            f.write(pad)


# ======================================================================
# Main
# ======================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Convert a PNG to 256-color indexed BMP using a reference palette.')
    parser.add_argument('input', help='Input PNG file')
    parser.add_argument('output', help='Output BMP file')
    parser.add_argument('-p', '--palette', default=None,
                        help='Reference palette BMP (default: palette.bmp or build/palette.bmp)')
    parser.add_argument('-t', '--threshold', type=int, default=128,
                        help='Alpha threshold 0-255 (default: 128). '
                             'Pixels with alpha < threshold become index 0.')
    args = parser.parse_args()

    # Find palette file
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    if args.palette:
        pal_path = args.palette
    elif os.path.exists(args.output):
        pal_path = args.output
    elif os.path.exists(os.path.join(project_dir, 'palette.bmp')):
        pal_path = os.path.join(project_dir, 'palette.bmp')
    elif os.path.exists(os.path.join(project_dir, 'build', 'palette.bmp')):
        pal_path = os.path.join(project_dir, 'build', 'palette.bmp')
    else:
        print('Error: no palette BMP found. Use -p to specify one, or run make_palette.py first.',
              file=sys.stderr)
        sys.exit(1)

    palette = read_bmp_palette(pal_path)
    width, height, pixels = read_png_rgba(args.input)

    # Build color cache to speed up repeated lookups
    cache = {}
    indices = []
    transparent = 0
    mapped = 0

    for r, g, b, a in pixels:
        if a < args.threshold:
            indices.append(0)
            transparent += 1
        else:
            key = (r, g, b)
            if key not in cache:
                cache[key] = find_closest(r, g, b, palette, start=1)
            indices.append(cache[key])
            mapped += 1

    write_indexed_bmp(args.output, width, height, palette, indices)

    print(f'{args.input} -> {args.output}  ({width}x{height}, '
          f'{mapped} pixels mapped, {transparent} transparent, '
          f'{len(cache)} unique colors)')


if __name__ == '__main__':
    main()
