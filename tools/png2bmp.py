#!/usr/bin/env python3
"""
Convert a PNG image to a 256-color indexed BMP.

Palette resolution, in priority order:
    1. -L MODE         build a Lambert-friendly palette (hsv | ramps)
    2. -q              always quantise a fresh palette from the input
    3. -p FILE         use the palette from FILE
    4. output exists   reuse the palette from the existing output BMP
    5. neither         quantise a new 255-colour palette from the input
                       (index 0 is reserved for transparent pixels)

Usage:
    python3 tools/png2bmp.py input.png output.bmp [options]

Options:
    -p, --palette FILE    Reference palette BMP (overrides auto-detect).
    -q, --quantize        Force a fresh quantised palette even when the
                          output file already exists.
    -t, --threshold INT   Alpha threshold 0-255 (default: 128).
                          Pixels with alpha < threshold become index 0.
    -L, --lambert MODE    Build a palette organised into hue ramps so
                          colormap_build()-driven Lambert lighting picks
                          shading neighbours along the same hue rather
                          than across hues. MODE is one of:
                            hsv   - bin input pixels by hue, quantise each
                                    bucket along the value axis. Stays
                                    close to the source's actual colours.
                            ramps - pick dominant hues, synthesise clean
                                    black->hue->white ramps. Doesn't
                                    preserve source colours but every hue
                                    gets a perfect ramp by construction.
                          Forces a fresh palette (overrides cached output).

No external dependencies.
"""

import argparse
import math
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
# Quantization (median cut)
# ======================================================================

def quantize(unique_colors, target):
    """Heckbert median cut. `unique_colors` is an iterable of (r,g,b)
    tuples; returns (representatives, color_to_idx) where:

      representatives  = list of up to `target` (r,g,b) tuples (centroids
                         of the resulting boxes)
      color_to_idx     = dict mapping every input colour to its 0-based
                         index into `representatives`

    Caller is expected to shift the indices if reserving index 0 for
    transparency."""
    boxes = [list(unique_colors)]
    if not boxes[0]:
        return [], {}

    while len(boxes) < target:
        # Find the box with the largest single-channel range; split it.
        best_i, best_ch, best_range = -1, 0, -1
        for i, box in enumerate(boxes):
            if len(box) <= 1:
                continue
            for ch in range(3):
                lo = min(c[ch] for c in box)
                hi = max(c[ch] for c in box)
                r = hi - lo
                if r > best_range:
                    best_range = r
                    best_i = i
                    best_ch = ch
        if best_i < 0:
            break  # all boxes are singletons
        box = boxes[best_i]
        box.sort(key=lambda c: c[best_ch])
        mid = len(box) // 2
        boxes[best_i] = box[:mid]
        boxes.append(box[mid:])

    reps = []
    color_to_idx = {}
    for idx, box in enumerate(boxes):
        n = len(box)
        cr = sum(c[0] for c in box) // n
        cg = sum(c[1] for c in box) // n
        cb = sum(c[2] for c in box) // n
        reps.append((cr, cg, cb))
        for c in box:
            color_to_idx[c] = idx
    return reps, color_to_idx


# ======================================================================
# Lambert-friendly palettes
# ======================================================================
#
# colormap_build() in the runtime turns a base palette into a 64-level
# brightness LUT by darkening/brightening each entry's RGB and finding
# the nearest existing palette entry. That only produces clean shading
# if the palette already contains darker / brighter neighbours of every
# colour. A median-cut palette typically gives each hue 1-2 entries,
# so Lambert-shaded surfaces drift across hues as they dim (a red on a
# mostly-blue palette ends up shading toward blue).
#
# Both modes here organise the palette into per-hue ramps so the LUT's
# nearest-entry search stays inside the source hue. They differ in how
# the ramp colours are chosen.


# Default number of hue strata / synthesised ramps. 16 gives ~16 entries
# per ramp out of a 255-entry budget, which is a comfortable Lambert
# range. Tunable in the algorithms below if a scene needs more or fewer.
LAMBERT_RAMPS = 16

# Saturation below this is treated as "neutral / grayscale" — the hue
# value is unstable in that range so we put those colours in their own
# bucket instead of forcing them into a hue ramp.
LAMBERT_NEUTRAL_S = 0.10


def rgb_to_hsv(r, g, b):
    """RGB (0-255 ints) -> HSV (h in [0, 360), s and v in [0, 1])."""
    rf, gf, bf = r / 255.0, g / 255.0, b / 255.0
    mx = max(rf, gf, bf)
    mn = min(rf, gf, bf)
    delta = mx - mn
    if delta == 0:
        h = 0.0
    elif mx == rf:
        h = 60.0 * (((gf - bf) / delta) % 6)
    elif mx == gf:
        h = 60.0 * ((bf - rf) / delta + 2)
    else:
        h = 60.0 * ((rf - gf) / delta + 4)
    s = 0.0 if mx == 0 else delta / mx
    return h, s, mx


def hsl_to_rgb(h, s, l):
    """HSL (h in [0, 360), s and l in [0, 1]) -> RGB (0-255 ints).
    Lightness L=0 is always black, L=1 is always white, so a hue+sat
    sweep along L produces a clean black->hue->white ramp."""
    c = (1.0 - abs(2.0 * l - 1.0)) * s
    hp = (h % 360.0) / 60.0
    x = c * (1.0 - abs(hp % 2.0 - 1.0))
    if hp < 1:
        r1, g1, b1 = c, x, 0.0
    elif hp < 2:
        r1, g1, b1 = x, c, 0.0
    elif hp < 3:
        r1, g1, b1 = 0.0, c, x
    elif hp < 4:
        r1, g1, b1 = 0.0, x, c
    elif hp < 5:
        r1, g1, b1 = x, 0.0, c
    else:
        r1, g1, b1 = c, 0.0, x
    m = l - c / 2.0
    return (max(0, min(255, int(round((r1 + m) * 255)))),
            max(0, min(255, int(round((g1 + m) * 255)))),
            max(0, min(255, int(round((b1 + m) * 255)))))


def _lambert_buckets(unique_colors):
    """Bin input colours by hue, with desaturated colours collected in
    a separate neutral bucket. Returns (chrom_buckets, neutrals, hsv_of)
    where chrom_buckets is a list of LAMBERT_RAMPS lists and neutrals
    is a flat list. unique_colors is sorted first for deterministic
    output across runs."""
    sorted_colors = sorted(unique_colors)
    hsv_of = {rgb: rgb_to_hsv(*rgb) for rgb in sorted_colors}
    chrom = [[] for _ in range(LAMBERT_RAMPS)]
    neutrals = []
    for rgb in sorted_colors:
        h, s, v = hsv_of[rgb]
        if s < LAMBERT_NEUTRAL_S:
            neutrals.append(rgb)
        else:
            chrom[int(h * LAMBERT_RAMPS / 360.0) % LAMBERT_RAMPS].append(rgb)
    return chrom, neutrals, hsv_of


def quantize_lambert_hsv(unique_colors, target):
    """Hue-stratified quantization. Each hue bucket is sub-quantized
    along its value (brightness) axis and contributes that many ramp
    entries. Output stays close to the source's actual colours;
    Lambert shading slides up and down the value range that's actually
    present in each hue.

    Empty hue bins contribute nothing. Budget is split evenly across
    non-empty bins (with leftover entries handed out one-per-bin)."""
    chrom, neutrals, hsv_of = _lambert_buckets(unique_colors)
    nonempty = [b for b in chrom if b]
    total = len(nonempty) + (1 if neutrals else 0)
    if total == 0:
        return [], {}

    base = target // total
    extra = target - base * total

    palette = []
    color_to_idx = {}

    def quantize_along_value(colors, steps):
        if not colors or steps == 0:
            return [], {}
        # Sort by V so equal-population slices give an ascending ramp.
        ordered = sorted(colors, key=lambda c: hsv_of[c][2])
        n = len(ordered)
        if n <= steps:
            # More budget than colours — emit them verbatim, sorted by V
            # so the ramp ordering is preserved.
            return list(ordered), {c: i for i, c in enumerate(ordered)}
        reps = []
        c2i = {}
        for i in range(steps):
            lo = i * n // steps
            hi = (i + 1) * n // steps
            slc = ordered[lo:hi]
            cr = sum(c[0] for c in slc) // len(slc)
            cg = sum(c[1] for c in slc) // len(slc)
            cb = sum(c[2] for c in slc) // len(slc)
            reps.append((cr, cg, cb))
            for c in slc:
                c2i[c] = i
        return reps, c2i

    def absorb(bucket):
        nonlocal extra
        share = base + (1 if extra > 0 else 0)
        if extra > 0:
            extra -= 1
        reps, c2i = quantize_along_value(bucket, share)
        offset = len(palette)
        for c, i in c2i.items():
            color_to_idx[c] = offset + i
        palette.extend(reps)

    for bucket in nonempty:
        absorb(bucket)
    if neutrals:
        absorb(neutrals)

    return palette, color_to_idx


def quantize_lambert_synth(unique_colors, target):
    """Synthesised Lambert ramps. Finds the dominant hues in the input
    and constructs a clean black -> pure hue -> white ramp for each
    via HSL. Doesn't preserve exact source colours, but every hue gets
    a flawless shading ramp by construction (no nearest-neighbour
    surprises).

    Better fit when the source is hand-painted with isolated accent
    colours; the HSV mode is closer to the source for natural images
    that already have wide colour distributions."""
    chrom, neutrals, _ = _lambert_buckets(unique_colors)

    # One anchor (mean hue, mean saturation) per non-empty hue bucket.
    anchors = []
    for bucket in chrom:
        if not bucket:
            continue
        # Saturation-weighted circular mean — hue from desaturated
        # colours pulls the mean toward their unstable angle, so weight
        # by S to keep dominant chromatic samples in charge.
        sx = sy = ws = 0.0
        sum_s = 0.0
        for rgb in bucket:
            h, s, _ = rgb_to_hsv(*rgb)
            w = s
            sx += math.cos(math.radians(h)) * w
            sy += math.sin(math.radians(h)) * w
            ws += w
            sum_s += s
        mean_h = math.degrees(math.atan2(sy, sx)) % 360
        mean_s = min(1.0, sum_s / len(bucket))
        anchors.append((mean_h, mean_s))

    total = len(anchors) + (1 if neutrals else 0)
    if total == 0:
        return [], {}

    base = target // total
    extra = target - base * total

    def synth_ramp(hue, sat, steps):
        if steps <= 0:
            return []
        if steps == 1:
            return [hsl_to_rgb(hue, sat, 0.5)]
        # L from 0 to 1: HSL gives black at 0, pure hue at 0.5, white
        # at 1. The resulting ramp covers the full Lambert range that
        # colormap_build() expects (level 0 = black, level 63 = white).
        return [hsl_to_rgb(hue, sat, i / (steps - 1)) for i in range(steps)]

    palette = []
    for hue, sat in anchors:
        share = base + (1 if extra > 0 else 0)
        if extra > 0:
            extra -= 1
        palette.extend(synth_ramp(hue, sat, share))
    if neutrals:
        share = base + (1 if extra > 0 else 0)
        # Saturation 0 collapses HSL to pure greyscale across L.
        palette.extend(synth_ramp(0.0, 0.0, share))

    # Map every input colour to its nearest synthesised entry. The
    # ramp colours are constructed, not sampled, so we have to do the
    # match here instead of inheriting it from the bucketing step.
    color_to_idx = {}
    for rgb in sorted(unique_colors):
        best_i = 0
        best_d = float('inf')
        r, g, b = rgb
        for i, (pr, pg, pb) in enumerate(palette):
            d = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb)
            if d < best_d:
                best_d = d
                best_i = i
                if d == 0:
                    break
        color_to_idx[rgb] = best_i
    return palette, color_to_idx


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
        description='Convert a PNG to 256-color indexed BMP. '
                    'Palette: -q > -p > existing output file > newly quantised.')
    parser.add_argument('input', help='Input PNG file')
    parser.add_argument('output', help='Output BMP file')
    parser.add_argument('-p', '--palette', default=None,
                        help='Reference palette BMP (overrides auto-detect).')
    parser.add_argument('-q', '--quantize', action='store_true',
                        help='Force fresh quantisation even if output exists.')
    parser.add_argument('-t', '--threshold', type=int, default=128,
                        help='Alpha threshold 0-255 (default: 128). '
                             'Pixels with alpha < threshold become index 0.')
    parser.add_argument('-L', '--lambert', choices=['hsv', 'ramps'],
                        default=None,
                        help='Build a Lambert-friendly palette: '
                             '"hsv" stratifies by hue and quantises along '
                             'value (preserves source colours); "ramps" '
                             'synthesises clean black-hue-white ramps from '
                             'dominant hues. Forces a fresh palette.')
    args = parser.parse_args()

    if args.lambert:
        if args.palette:
            sys.exit('--lambert is incompatible with -p/--palette '
                     '(an external palette overrides palette generation).')
        pal_source = None
    elif args.quantize:
        pal_source = None
    elif args.palette:
        pal_source = args.palette
    elif os.path.exists(args.output):
        pal_source = args.output
    else:
        pal_source = None  # quantise from input

    width, height, pixels = read_png_rgba(args.input)

    indices = []
    transparent = 0
    mapped = 0

    if pal_source is not None:
        palette = read_bmp_palette(pal_source)
        cache = {}
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
        unique_count = len(cache)
        pal_note = f'palette={pal_source}'
    else:
        # Build a fresh palette from the non-transparent unique colours.
        # Index 0 is reserved for transparent so we ask each algorithm
        # for 255 entries and prepend (0, 0, 0).
        unique = set()
        for r, g, b, a in pixels:
            if a >= args.threshold:
                unique.add((r, g, b))

        if args.lambert == 'hsv':
            reps, color_to_idx = quantize_lambert_hsv(unique, 255)
            pal_note = f'palette=lambert-hsv ({len(reps)} entries)'
        elif args.lambert == 'ramps':
            reps, color_to_idx = quantize_lambert_synth(unique, 255)
            pal_note = f'palette=lambert-ramps ({len(reps)} entries)'
        else:
            reps, color_to_idx = quantize(unique, 255)
            pal_note = f'palette=quantised({len(reps)} colours)'

        palette = [(0, 0, 0)] + reps
        while len(palette) < 256:
            palette.append((0, 0, 0))
        for r, g, b, a in pixels:
            if a < args.threshold:
                indices.append(0)
                transparent += 1
            else:
                indices.append(1 + color_to_idx[(r, g, b)])
                mapped += 1
        unique_count = len(unique)

    write_indexed_bmp(args.output, width, height, palette, indices)

    print(f'{args.input} -> {args.output}  ({width}x{height}, '
          f'{mapped} pixels mapped, {transparent} transparent, '
          f'{unique_count} unique colours, {pal_note})')


if __name__ == '__main__':
    main()
