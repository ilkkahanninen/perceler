#!/usr/bin/env python3
"""
Encode a Perceler capture (.RAW + .WAV pair) into a video file via ffmpeg.

The .RAW holds a stream of indexed frames (320×200 @ 8 bits + palette);
this script expands each frame to RGB24 and pipes it to ffmpeg's stdin.
The companion .WAV is passed as a regular audio input. No intermediate
PNG files.

Defaults are tuned for mastering quality: libx264 `-crf 10 -preset slower
-tune animation`, 320 kb/s AAC audio, `+faststart`. Files are large — the
assumption is that YouTube / Vimeo / etc. will re-encode anyway, so we
keep as much headroom as possible on the source.

Usage:
    # Mastering-quality defaults, native resolution.
    python3 tools/capture2video.py CAPTURE demo.mp4

    # 3× nearest-neighbour upscale for a friendlier upload resolution.
    python3 tools/capture2video.py --scale 3 CAPTURE demo.mp4

    # True lossless master (enormous file).
    python3 tools/capture2video.py --lossless CAPTURE demo.mp4

    # Smaller / faster encode for preview.
    python3 tools/capture2video.py --crf 22 --preset fast CAPTURE demo.mp4

The `input` argument accepts either the capture prefix (so CAPTURE.RAW
and CAPTURE.WAV are both picked up) or a direct path to the .RAW file.

Requires ffmpeg on PATH. Install it with:
    macOS:  brew install ffmpeg
    Linux:  apt/dnf/pacman install ffmpeg
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys

WIDTH = 320
HEIGHT = 200
FRAME_PAL_BYTES = 768
FRAME_PIX_BYTES = WIDTH * HEIGHT
FRAME_BYTES = FRAME_PAL_BYTES + FRAME_PIX_BYTES
DEFAULT_FRAMERATE = 60


def resolve_paths(prefix):
    """Return (raw_path, wav_path) for the given prefix or .RAW path."""
    if prefix.lower().endswith('.raw'):
        raw = prefix
        wav = prefix[:-4] + '.WAV'
        if not os.path.isfile(wav):
            # Lower-case fallback.
            wav = prefix[:-4] + '.wav'
    else:
        raw = prefix + '.RAW'
        wav = prefix + '.WAV'
        if not os.path.isfile(raw):
            raw = prefix + '.raw'
        if not os.path.isfile(wav):
            wav = prefix + '.wav'
    if not os.path.isfile(raw):
        sys.exit('capture file not found: %s' % raw)
    if not os.path.isfile(wav):
        sys.exit('audio file not found: %s' % wav)
    return raw, wav


# Precomputed VGA (0-63) → 8-bit lookup with bit replication.
VGA_LUT = bytes(((v & 0x3F) << 2) | ((v & 0x3F) >> 4) for v in range(256))


def main():
    ap = argparse.ArgumentParser(
        description='Encode a Perceler capture to a video file via ffmpeg.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument('input',
                    help='Capture prefix (CAPTURE → CAPTURE.RAW + CAPTURE.WAV) '
                         'or direct path to the .RAW file.')
    ap.add_argument('output', help='Output video file (e.g. demo.mp4)')
    ap.add_argument('--framerate', type=int, default=DEFAULT_FRAMERATE,
                    help='Frame rate of the capture (default: 60)')
    ap.add_argument('--scale', type=int, default=1,
                    help='Integer nearest-neighbour upscale factor '
                         '(default: 1; 3-6 is a good range for upload)')
    ap.add_argument('--crf', type=int, default=10,
                    help='libx264 CRF quality, lower = higher quality; '
                         'default: 10 (mastering quality — upload will be '
                         're-encoded anyway so extra headroom matters)')
    ap.add_argument('--preset', default='slower',
                    help='libx264 preset; slower = better compression at '
                         'the same quality (default: slower)')
    ap.add_argument('--tune', default='animation',
                    help='libx264 tune; animation tightens deblocking for '
                         'flat-colour pixel art (default: animation)')
    ap.add_argument('--lossless', action='store_true',
                    help='Encode truly lossless (-qp 0); big files, best '
                         'possible archival master')
    args = ap.parse_args()

    if not shutil.which('ffmpeg'):
        sys.exit('ffmpeg not found on PATH. Install it first:\n'
                 '    macOS:  brew install ffmpeg\n'
                 '    Linux:  apt/dnf/pacman install ffmpeg')

    raw_path, wav_path = resolve_paths(args.input)

    scale_filter = []
    if args.scale != 1:
        scale_filter = ['-vf', 'scale=%d:%d:flags=neighbor'
                        % (WIDTH * args.scale, HEIGHT * args.scale)]

    quality_args = ['-qp', '0'] if args.lossless else ['-crf', str(args.crf)]

    cmd = [
        'ffmpeg', '-y', '-hide_banner',
        # Raw video input on stdin.
        '-f', 'rawvideo',
        '-pixel_format', 'rgb24',
        '-video_size', '%dx%d' % (WIDTH, HEIGHT),
        '-framerate', str(args.framerate),
        '-i', '-',
        # Audio input.
        '-i', wav_path,
    ] + scale_filter + [
        '-c:v', 'libx264',
        '-preset', args.preset,
        '-tune', args.tune,
        '-pix_fmt', 'yuv420p',
    ] + quality_args + [
        '-c:a', 'aac', '-b:a', '320k',
        '-movflags', '+faststart',
        '-shortest',
        args.output,
    ]

    print('ffmpeg:', ' '.join(cmd), file=sys.stderr)

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    frame_num = 0
    try:
        with open(raw_path, 'rb') as f:
            magic = f.read(4)
            if magic != b'PRCL':
                proc.stdin.close()
                proc.wait()
                sys.exit('%s: missing PRCL magic header' % raw_path)

            while True:
                chunk = f.read(FRAME_BYTES)
                if not chunk:
                    break
                if len(chunk) < FRAME_BYTES:
                    print('warning: truncated frame %d, stopping'
                          % frame_num, file=sys.stderr)
                    break

                # Build a 256-entry table mapping pixel-index -> 3-byte RGB.
                # The VGA DAC palette is 6-bit; upscale with bit replication.
                pal_raw = chunk[:FRAME_PAL_BYTES]
                rgb_pal = pal_raw.translate(VGA_LUT)
                pal_table = [rgb_pal[i * 3:i * 3 + 3] for i in range(256)]

                pixels = chunk[FRAME_PAL_BYTES:]
                frame_rgb = b''.join(pal_table[idx] for idx in pixels)

                try:
                    proc.stdin.write(frame_rgb)
                except BrokenPipeError:
                    print('ffmpeg closed the pipe early', file=sys.stderr)
                    break
                frame_num += 1
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        rc = proc.wait()

    if rc != 0:
        sys.exit('ffmpeg exited with code %d' % rc)

    print('%s + %s -> %s: %d frames'
          % (raw_path, wav_path, args.output, frame_num))


if __name__ == '__main__':
    main()
