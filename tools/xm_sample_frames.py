#!/usr/bin/env python3
"""
Find the 60fps frame numbers where given samples are triggered in an XM file.

Parses the XM module, walks through the pattern order row by row, tracks
tempo (BPM) and speed changes, and outputs a C header with frame arrays
for each requested sample.

Usage:
    python3 tools/xm_sample_frames.py song.xm output.h 1 5 12
    python3 tools/xm_sample_frames.py song.xm --list

    --list   List all instruments/samples in the module and exit.

No external dependencies.
"""

import struct
import sys
import os

NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-",
              "F#", "G-", "G#", "A-", "A#", "B-"]


def note_str(note):
    if note == 97:
        return "OFF"
    if note < 1 or note > 96:
        return "---"
    n = note - 1
    return f"{NOTE_NAMES[n % 12]}{n // 12}"


def read_xm(path):
    with open(path, "rb") as f:
        data = f.read()

    # Header
    header_size = struct.unpack_from("<I", data, 60)[0]
    song_length = struct.unpack_from("<H", data, 64)[0]
    num_channels = struct.unpack_from("<H", data, 68)[0]
    num_patterns = struct.unpack_from("<H", data, 70)[0]
    num_instruments = struct.unpack_from("<H", data, 72)[0]
    default_speed = struct.unpack_from("<H", data, 76)[0]
    default_bpm = struct.unpack_from("<H", data, 78)[0]

    order_table = list(data[80:80 + song_length])

    # Parse patterns
    offset = 60 + header_size
    patterns = []

    for _ in range(num_patterns):
        pat_header_len = struct.unpack_from("<I", data, offset)[0]
        num_rows = struct.unpack_from("<H", data, offset + 5)[0]
        packed_size = struct.unpack_from("<H", data, offset + 7)[0]
        offset += pat_header_len

        rows = []
        pos = offset
        end = offset + packed_size

        for _ in range(num_rows):
            row = []
            for _ in range(num_channels):
                note = 0
                instrument = 0
                volume = 0
                effect_type = 0
                effect_param = 0

                if pos < end:
                    first = data[pos]
                    pos += 1

                    if first & 0x80:
                        # Packed
                        if first & 0x01:
                            note = data[pos]; pos += 1
                        if first & 0x02:
                            instrument = data[pos]; pos += 1
                        if first & 0x04:
                            volume = data[pos]; pos += 1
                        if first & 0x08:
                            effect_type = data[pos]; pos += 1
                        if first & 0x10:
                            effect_param = data[pos]; pos += 1
                    else:
                        # Unpacked: first byte is the note
                        note = first
                        instrument = data[pos]; pos += 1
                        volume = data[pos]; pos += 1
                        effect_type = data[pos]; pos += 1
                        effect_param = data[pos]; pos += 1

                row.append((note, instrument, volume, effect_type, effect_param))
            rows.append(row)

        patterns.append(rows)
        offset = end

    # Parse instruments (just names for --list)
    instruments = []
    for _ in range(num_instruments):
        inst_size = struct.unpack_from("<I", data, offset)[0]
        name = data[offset + 4:offset + 26].split(b'\x00')[0].decode("ascii", errors="replace").strip()
        num_samples = struct.unpack_from("<H", data, offset + 27)[0]
        offset += inst_size

        sample_sizes = []
        if num_samples > 0:
            for s in range(num_samples):
                smp_len = struct.unpack_from("<I", data, offset + s * 40)[0]
                sample_sizes.append(smp_len)
            offset += num_samples * 40
            for sl in sample_sizes:
                offset += sl

        instruments.append(name)

    return {
        "song_length": song_length,
        "num_channels": num_channels,
        "default_speed": default_speed,
        "default_bpm": default_bpm,
        "order_table": order_table,
        "patterns": patterns,
        "instruments": instruments,
    }


def find_sample_frames(xm, sample_set):
    FPS = 60.0
    speed = xm["default_speed"]
    bpm = xm["default_bpm"]
    ms = 0.0
    results = {}
    for s in sample_set:
        results[s] = []

    for order_idx, pat_idx in enumerate(xm["order_table"]):
        if pat_idx >= len(xm["patterns"]):
            continue
        pattern = xm["patterns"][pat_idx]

        for row_idx, row in enumerate(pattern):
            frame = int(ms * FPS / 1000.0 + 0.5)

            for ch, (note, instrument, volume, fx, fx_param) in enumerate(row):
                if note >= 1 and note <= 96 and instrument in sample_set:
                    results[instrument].append(frame)

                # Track speed/tempo changes
                if fx == 0x0F:
                    if fx_param < 0x20:
                        speed = fx_param if fx_param > 0 else speed
                    else:
                        bpm = fx_param

            row_ms = speed * 2500.0 / bpm
            ms += row_ms

    return results


def write_header(out_path, xm_path, results):
    guard = os.path.basename(out_path).upper().replace(".", "_").replace("-", "_")
    xm_name = os.path.basename(xm_path)

    with open(out_path, "w") as f:
        f.write(f"/* Sample trigger frames from {xm_name} at 60 fps */\n")
        f.write(f"/* Generated by xm_sample_frames.py */\n\n")
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")

        for sample_num in sorted(results.keys()):
            frames = results[sample_num]
            name = f"sample_{sample_num}_frames"
            f.write(f"static const unsigned int {name}[] = {{\n")
            for i in range(0, len(frames), 12):
                chunk = frames[i:i + 12]
                line = ", ".join(str(fr) for fr in chunk)
                f.write(f"    {line},\n")
            f.write(f"}};\n")
            f.write(f"#define {name.upper()}_COUNT {len(frames)}\n\n")

        f.write(f"#endif\n")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <file.xm> <output.h> <sample numbers...>",
              file=sys.stderr)
        print(f"       {sys.argv[0]} <file.xm> --list", file=sys.stderr)
        sys.exit(1)

    xm_path = sys.argv[1]
    xm = read_xm(xm_path)

    if sys.argv[2] == "--list":
        print(f"Channels: {xm['num_channels']}, "
              f"Speed: {xm['default_speed']}, BPM: {xm['default_bpm']}")
        print(f"Song length: {xm['song_length']} orders\n")
        print("Instruments:")
        for i, name in enumerate(xm["instruments"]):
            label = f"  {i + 1:3d}: {name}" if name else f"  {i + 1:3d}: (empty)"
            print(label)
        return

    if len(sys.argv) < 4:
        print("Error: specify output .h path and sample numbers", file=sys.stderr)
        sys.exit(1)

    out_path = sys.argv[2]
    sample_set = set()
    for arg in sys.argv[3:]:
        sample_set.add(int(arg))

    results = find_sample_frames(xm, sample_set)
    write_header(out_path, xm_path, results)

    for sample_num in sorted(results.keys()):
        print(f"sample {sample_num}: {len(results[sample_num])} hits",
              file=sys.stderr)


if __name__ == "__main__":
    main()
