#!/usr/bin/env python3
"""
Generate a 256-color indexed BMP palette image for importing into GIMP.

Usage:
    python3 tools/make_palette.py [output.bmp]

The palette is defined in the palette() function below — edit it to
create your own.  Each entry is an (R, G, B) tuple with values 0-255.

The output is a 16x16 BMP where each pixel shows one palette index,
making it easy to verify visually and import into GIMP via:
    Image → Mode → Indexed → Use custom palette
"""

import struct
import sys


# ======================================================================
# Edit this function to define your palette.
# Return a list of 256 (R, G, B) tuples, values 0-255.
# ======================================================================
def palette():
    pal = [(0, 0, 0)] * 256

    for i in range(16):
        for j in range(16):
            r = (i * 255) // 15
            g = (j * 255) // 15
            b = ((i + j) * 255) // 30
            pal[i * 16 + j] = (r, g, b)

    return pal


# ======================================================================
# BMP writer — no external dependencies
# ======================================================================
def write_indexed_bmp(filename, pal):
    """Write a 16x16 8-bit indexed BMP where pixel (x, y) = index y*16+x."""
    width, height = 16, 16
    stride = (width + 3) & ~3  # row padded to 4 bytes (16 is already aligned)
    pixel_data_size = stride * height
    color_table_size = 256 * 4
    header_size = 14 + 40  # file header + info header
    pixel_offset = header_size + color_table_size
    file_size = pixel_offset + pixel_data_size

    with open(filename, 'wb') as f:
        # -- File header (14 bytes) --
        f.write(b'BM')
        f.write(struct.pack('<I', file_size))
        f.write(struct.pack('<HH', 0, 0))       # reserved
        f.write(struct.pack('<I', pixel_offset))

        # -- Info header (40 bytes) --
        f.write(struct.pack('<I', 40))           # header size
        f.write(struct.pack('<i', width))
        f.write(struct.pack('<i', height))
        f.write(struct.pack('<HH', 1, 8))        # planes=1, bits=8
        f.write(struct.pack('<I', 0))             # compression=BI_RGB
        f.write(struct.pack('<I', pixel_data_size))
        f.write(struct.pack('<i', 0))             # X pix/meter
        f.write(struct.pack('<i', 0))             # Y pix/meter
        f.write(struct.pack('<I', 256))           # colors used
        f.write(struct.pack('<I', 0))             # colors important

        # -- Color table (256 * BGRA) --
        for r, g, b in pal:
            f.write(struct.pack('BBBB', b, g, r, 0))

        # -- Pixel data (bottom-to-top) --
        for row in range(height - 1, -1, -1):
            for col in range(width):
                f.write(struct.pack('B', row * 16 + col))
            # pad to stride (16 is already a multiple of 4, so no padding)
            f.write(b'\x00' * (stride - width))


if __name__ == '__main__':
    output = sys.argv[1] if len(sys.argv) > 1 else 'assets/palette.bmp'
    pal = palette()
    assert len(pal) == 256, f"Palette must have exactly 256 entries, got {len(pal)}"
    write_indexed_bmp(output, pal)
    print(f"Wrote {output}  (256-color 16x16 indexed BMP)")
