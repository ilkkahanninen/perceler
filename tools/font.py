#!/usr/bin/env python3
"""
Create drawing templates and convert filled-in BMPs to the binary .fnt
asset format consumed by the runtime font loader.

Subcommands:
    template - create an empty template BMP with grid cells + labels
    build    - convert a filled-in template BMP into a .fnt asset

Both subcommands take --width / --height / --first / --count to
describe the font geometry; the cell layout is computed identically.

Typical flow:
    python3 tools/font.py template --width 16 --height 16 mystyle.bmp
    # open mystyle.bmp in a pixel editor, paint glyph canvases with
    # palette index 1 (white); leave borders / labels / reference alone
    python3 tools/font.py build --width 16 --height 16 mystyle.bmp \\
        assets/mystyle.fnt

.fnt binary layout (little-endian):
    offset  size  field
      0     4    glyph_w     (int32, must be a multiple of 8)
      4     4    glyph_h     (int32)
      8     4    first_char  (int32)
     12     4    num_chars   (int32)
     16     N    glyph data  (num_chars * (glyph_w/8) * glyph_h bytes,
                              each row LSB-first)

Template-BMP palette convention:
    0  black  - canvas background (off pixel in output)
    1  white  - draw here (on pixel in output)
    2  gray   - borders, labels, reference glyph (ignored by `build`)

No external dependencies.
"""

import argparse
import os
import struct
import sys


# ---------------------------------------------------------------------------
# Embedded 8x8 reference font (matches font_default in src/scenes/utils/font.c)
# ---------------------------------------------------------------------------
REF_FONT_FIRST = 32
REF_FONT = [
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # 0x20 ' '
    0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00,  # 0x21 '!'
    0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # 0x22 '"'
    0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00,  # 0x23 '#'
    0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00,  # 0x24 '$'
    0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00,  # 0x25 '%'
    0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00,  # 0x26 '&'
    0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,  # 0x27 "'"
    0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00,  # 0x28 '('
    0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00,  # 0x29 ')'
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00,  # 0x2A '*'
    0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00,  # 0x2B '+'
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06,  # 0x2C ','
    0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00,  # 0x2D '-'
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00,  # 0x2E '.'
    0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00,  # 0x2F '/'
    0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00,  # 0x30 '0'
    0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00,  # 0x31 '1'
    0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00,  # 0x32 '2'
    0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00,  # 0x33 '3'
    0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00,  # 0x34 '4'
    0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00,  # 0x35 '5'
    0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00,  # 0x36 '6'
    0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00,  # 0x37 '7'
    0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00,  # 0x38 '8'
    0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00,  # 0x39 '9'
    0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00,  # 0x3A ':'
    0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06,  # 0x3B ';'
    0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00,  # 0x3C '<'
    0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00,  # 0x3D '='
    0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00,  # 0x3E '>'
    0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00,  # 0x3F '?'
    0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00,  # 0x40 '@'
    0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00,  # 0x41 'A'
    0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00,  # 0x42 'B'
    0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00,  # 0x43 'C'
    0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00,  # 0x44 'D'
    0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00,  # 0x45 'E'
    0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00,  # 0x46 'F'
    0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00,  # 0x47 'G'
    0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00,  # 0x48 'H'
    0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00,  # 0x49 'I'
    0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00,  # 0x4A 'J'
    0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00,  # 0x4B 'K'
    0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00,  # 0x4C 'L'
    0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00,  # 0x4D 'M'
    0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00,  # 0x4E 'N'
    0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00,  # 0x4F 'O'
    0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00,  # 0x50 'P'
    0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00,  # 0x51 'Q'
    0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00,  # 0x52 'R'
    0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00,  # 0x53 'S'
    0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00,  # 0x54 'T'
    0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00,  # 0x55 'U'
    0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00,  # 0x56 'V'
    0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00,  # 0x57 'W'
    0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00,  # 0x58 'X'
    0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00,  # 0x59 'Y'
    0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00,  # 0x5A 'Z'
    0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00,  # 0x5B '['
    0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00,  # 0x5C '\'
    0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00,  # 0x5D ']'
    0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00,  # 0x5E '^'
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,  # 0x5F '_'
    0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,  # 0x60 '`'
    0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00,  # 0x61 'a'
    0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00,  # 0x62 'b'
    0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00,  # 0x63 'c'
    0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00,  # 0x64 'd'
    0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00,  # 0x65 'e'
    0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00,  # 0x66 'f'
    0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F,  # 0x67 'g'
    0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00,  # 0x68 'h'
    0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00,  # 0x69 'i'
    0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E,  # 0x6A 'j'
    0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00,  # 0x6B 'k'
    0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00,  # 0x6C 'l'
    0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00,  # 0x6D 'm'
    0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00,  # 0x6E 'n'
    0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00,  # 0x6F 'o'
    0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F,  # 0x70 'p'
    0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78,  # 0x71 'q'
    0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00,  # 0x72 'r'
    0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00,  # 0x73 's'
    0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00,  # 0x74 't'
    0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00,  # 0x75 'u'
    0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00,  # 0x76 'v'
    0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00,  # 0x77 'w'
    0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00,  # 0x78 'x'
    0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F,  # 0x79 'y'
    0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00,  # 0x7A 'z'
    0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00,  # 0x7B '{'
    0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00,  # 0x7C '|'
    0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00,  # 0x7D '}'
    0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # 0x7E '~'
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # 0x7F
]

IDX_BG = 0
IDX_ON = 1
IDX_GUIDE = 2

LABEL_H = 8          # height of the ASCII-code label strip
GRID_COLS = 16       # cells per row


# ---------------------------------------------------------------------------
# Layout — both subcommands derive cell geometry from W, H.
# ---------------------------------------------------------------------------
def cell_geometry(glyph_w, glyph_h):
    """Return (cell_w, cell_h, canvas_x, canvas_y) for one grid cell.

    Canvas = the WxH drawable area that will be harvested by `build`.
    The cell is widened if needed to fit a 3-digit ASCII label in the header.
    """
    label_w_px = 3 * 8                    # up to 3 decimal digits
    inner_w = max(glyph_w, label_w_px)
    cell_w = inner_w + 2                  # +2 for left+right gray border
    cell_h = 1 + LABEL_H + 1 + glyph_h + 1  # border, label, separator, H, border
    canvas_x = 1 + (inner_w - glyph_w) // 2
    canvas_y = 1 + LABEL_H + 1
    return cell_w, cell_h, canvas_x, canvas_y


def grid_dims(count):
    cols = GRID_COLS
    rows = (count + cols - 1) // cols
    return cols, rows


# ---------------------------------------------------------------------------
# Reference-font rendering helpers (used by the `template` subcommand)
# ---------------------------------------------------------------------------
def ref_glyph_bytes(ch):
    idx = ch - REF_FONT_FIRST
    if idx < 0 or idx * 8 + 8 > len(REF_FONT):
        return [0] * 8
    return REF_FONT[idx * 8:idx * 8 + 8]


def put_ref_char(pixels, x, y, ch, color):
    bits = ref_glyph_bytes(ch)
    for row in range(8):
        b = bits[row]
        if not b:
            continue
        for col in range(8):
            if b & (1 << col):
                py, px = y + row, x + col
                if 0 <= py < len(pixels) and 0 <= px < len(pixels[0]):
                    pixels[py][px] = color


def put_ref_string(pixels, x, y, s, color):
    for i, c in enumerate(s):
        put_ref_char(pixels, x + i * 8, y, ord(c), color)


# ---------------------------------------------------------------------------
# BMP read/write (8-bit indexed, stdlib only)
# ---------------------------------------------------------------------------
def write_bmp_8bit(path, pixels, palette):
    """Write an 8-bit indexed BMP. palette is a list of (r, g, b) tuples."""
    h = len(pixels)
    w = len(pixels[0]) if h else 0
    row_stride = (w + 3) // 4 * 4
    pad = row_stride - w
    pal = list(palette) + [(0, 0, 0)] * (256 - len(palette))
    pal_bytes = b''.join(struct.pack('<BBBB', b, g, r, 0) for (r, g, b) in pal)
    pixel_bytes_size = row_stride * h
    data_offset = 14 + 40 + len(pal_bytes)
    file_size = data_offset + pixel_bytes_size

    with open(path, 'wb') as f:
        f.write(b'BM')
        f.write(struct.pack('<I', file_size))
        f.write(struct.pack('<HH', 0, 0))
        f.write(struct.pack('<I', data_offset))
        f.write(struct.pack('<IiiHHIIiiII',
                            40, w, h, 1, 8, 0, pixel_bytes_size,
                            0, 0, 256, 0))
        f.write(pal_bytes)
        # BMP stores rows bottom-up when height > 0.
        padding = b'\x00' * pad
        for row in reversed(pixels):
            f.write(bytes(row))
            f.write(padding)


def read_bmp_8bit(path):
    """Return (pixels, width, height). pixels[0] is the TOP row."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'BM':
        raise ValueError('%s: not a BMP file' % path)
    data_off = struct.unpack_from('<I', data, 10)[0]
    w = struct.unpack_from('<i', data, 18)[0]
    h_signed = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    compression = struct.unpack_from('<I', data, 30)[0]
    if bpp != 8:
        raise ValueError('%s: expected 8-bit indexed BMP, got %d-bit'
                         % (path, bpp))
    if compression != 0:
        raise ValueError('%s: compressed BMP not supported' % path)
    h = abs(h_signed)
    top_down = h_signed < 0
    row_stride = (w + 3) // 4 * 4
    rows = [None] * h
    for ri in range(h):
        off = data_off + ri * row_stride
        row = list(data[off:off + w])
        if top_down:
            rows[ri] = row
        else:
            rows[h - 1 - ri] = row
    return rows, w, h


# ---------------------------------------------------------------------------
# `template` subcommand
# ---------------------------------------------------------------------------
def cmd_template(args):
    W = args.width
    H = args.height
    first = args.first
    count = args.count

    if W <= 0 or W % 8 != 0:
        sys.exit('error: --width must be a positive multiple of 8')
    if H <= 0 or count <= 0:
        sys.exit('error: --height and --count must be positive')
    if first < 0 or first + count > 256:
        sys.exit('error: ASCII range must fit in 0..255')

    cell_w, cell_h, canvas_x, canvas_y = cell_geometry(W, H)
    cols, rows = grid_dims(count)
    img_w = cols * cell_w
    img_h = rows * cell_h

    pixels = [[IDX_BG] * img_w for _ in range(img_h)]

    for i in range(count):
        ch = first + i
        cx0 = (i % cols) * cell_w
        cy0 = (i // cols) * cell_h

        # Cell border (gray): top, bottom, left, right edges.
        for x in range(cx0, cx0 + cell_w):
            pixels[cy0][x] = IDX_GUIDE
            pixels[cy0 + cell_h - 1][x] = IDX_GUIDE
        for y in range(cy0, cy0 + cell_h):
            pixels[y][cx0] = IDX_GUIDE
            pixels[y][cx0 + cell_w - 1] = IDX_GUIDE

        # ASCII code label, 3 digits padded with leading spaces.
        label = '%3d' % ch
        put_ref_string(pixels, cx0 + 1, cy0 + 1, label, IDX_GUIDE)

        # Separator between label and canvas.
        sep_y = cy0 + 1 + LABEL_H
        for x in range(cx0, cx0 + cell_w):
            pixels[sep_y][x] = IDX_GUIDE

        # Canvas fill: keep as IDX_BG (black, off); overlay a centered
        # gray 8x8 reference glyph as a drawing guide.
        ref_x = cx0 + canvas_x + max(0, (W - 8) // 2)
        ref_y = cy0 + canvas_y + max(0, (H - 8) // 2)
        put_ref_char(pixels, ref_x, ref_y, ch, IDX_GUIDE)

    palette = [
        (0, 0, 0),          # 0 BG
        (255, 255, 255),    # 1 ON (paint here)
        (96, 96, 96),       # 2 GUIDE (borders/labels/reference)
    ]
    write_bmp_8bit(args.output, pixels, palette)
    print('%s: %dx%d template, %d cells (%dx%d canvas each)'
          % (args.output, img_w, img_h, count, W, H))


# ---------------------------------------------------------------------------
# `build` subcommand
# ---------------------------------------------------------------------------
def cmd_build(args):
    W = args.width
    H = args.height
    first = args.first
    count = args.count

    if W <= 0 or W % 8 != 0:
        sys.exit('error: --width must be a positive multiple of 8')
    if H <= 0 or count <= 0:
        sys.exit('error: --height and --count must be positive')

    cell_w, cell_h, canvas_x, canvas_y = cell_geometry(W, H)
    cols, rows = grid_dims(count)
    expected_w = cols * cell_w
    expected_h = rows * cell_h

    pixels, img_w, img_h = read_bmp_8bit(args.input)
    if img_w != expected_w or img_h != expected_h:
        sys.exit('error: %s is %dx%d, expected %dx%d for %dx%d glyphs '
                 '(count=%d, first=%d). Re-run with matching --width/--height/'
                 '--first/--count.'
                 % (args.input, img_w, img_h, expected_w, expected_h,
                    W, H, count, first))

    bytes_per_row = W // 8
    glyph_data = bytearray(count * bytes_per_row * H)

    on_pixels = 0
    for i in range(count):
        cx0 = (i % cols) * cell_w
        cy0 = (i // cols) * cell_h
        g_off = i * bytes_per_row * H
        for row in range(H):
            py = cy0 + canvas_y + row
            for byte_idx in range(bytes_per_row):
                byte = 0
                for bit in range(8):
                    col = byte_idx * 8 + bit
                    px = cx0 + canvas_x + col
                    if pixels[py][px] == IDX_ON:
                        byte |= 1 << bit
                        on_pixels += 1
                glyph_data[g_off + row * bytes_per_row + byte_idx] = byte

    with open(args.output, 'wb') as f:
        f.write(struct.pack('<iiii', W, H, first, count))
        f.write(glyph_data)

    header_bytes = 16
    total = header_bytes + len(glyph_data)
    print('%s -> %s: %d glyphs, %d bytes (%d header + %d data), %d on-pixels'
          % (args.input, args.output, count, total, header_bytes,
             len(glyph_data), on_pixels))


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(
        description='Create font templates and compile them to .fnt assets.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    sub = ap.add_subparsers(dest='cmd')
    sub.required = True

    t = sub.add_parser('template',
                       help='Generate an empty template BMP to draw glyphs in.')
    t.add_argument('--width', type=int, required=True,
                   help='Glyph width in pixels (multiple of 8)')
    t.add_argument('--height', type=int, required=True,
                   help='Glyph height in pixels')
    t.add_argument('--first', type=int, default=32,
                   help='ASCII code of first character (default: 32)')
    t.add_argument('--count', type=int, default=96,
                   help='Number of characters (default: 96)')
    t.add_argument('output', help='Output BMP path')
    t.set_defaults(func=cmd_template)

    b = sub.add_parser('build',
                       help='Convert a filled-in template BMP to a .fnt asset.')
    b.add_argument('--width', type=int, required=True,
                   help='Glyph width in pixels (multiple of 8)')
    b.add_argument('--height', type=int, required=True,
                   help='Glyph height in pixels')
    b.add_argument('--first', type=int, default=32,
                   help='ASCII code of first character (default: 32)')
    b.add_argument('--count', type=int, default=96,
                   help='Number of characters (default: 96)')
    b.add_argument('input', help='Input filled-in BMP path')
    b.add_argument('output', help='Output .fnt asset path')
    b.set_defaults(func=cmd_build)

    args = ap.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
