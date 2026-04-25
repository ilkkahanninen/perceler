#!/usr/bin/env python3
"""
Render a WAV file into a per-frame FFT band-energy track.

Pipeline:
  1. Load the WAV (stereo is averaged to mono).
  2. Peak-normalise so the loudest sample sits at full scale.
  3. For every video frame (60 fps by default), compute a windowed
     FFT and sum bin magnitudes inside [--low, --high] Hz.
  4. Normalise the per-frame energies to 0..255 and write each as a
     single byte to the output file.

Output is a header-less stream of bytes; one byte per frame, in order.
File length therefore equals the number of frames.

Requires numpy.
"""

import argparse
import os
import sys
import wave

try:
    import numpy as np
except ImportError:
    sys.stderr.write(
        "wav2fft.py requires numpy. Install it with `pip install numpy`.\n")
    sys.exit(1)


def read_wav_mono(path):
    with wave.open(path, "rb") as wf:
        n_channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        n_frames = wf.getnframes()
        raw = wf.readframes(n_frames)

    if sample_width == 1:
        # 8-bit unsigned, centred on 128
        s = np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0
        s /= 128.0
    elif sample_width == 2:
        s = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    elif sample_width == 4:
        s = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
    else:
        raise ValueError("unsupported sample width: %d bytes" % sample_width)

    if n_channels > 1:
        s = s.reshape(-1, n_channels).mean(axis=1)
    return s, sample_rate


def main():
    ap = argparse.ArgumentParser(
        description="WAV -> per-frame FFT band amplitude (8-bit binary)")
    ap.add_argument("input", help="input WAV file")
    ap.add_argument("output", nargs="?", default=None,
                    help="output binary path (default: assets/<stem>.fft)")
    ap.add_argument("--fps", type=int, default=60,
                    help="frames per second (default 60)")
    ap.add_argument("--low", type=float, default=0.0,
                    help="low cutoff in Hz (default 0)")
    ap.add_argument("--high", type=float, default=None,
                    help="high cutoff in Hz (default Nyquist)")
    ap.add_argument("--window", type=int, default=2048,
                    help="FFT window size, power of 2 (default 2048)")
    args = ap.parse_args()

    if args.output is None:
        stem = os.path.splitext(os.path.basename(args.input))[0]
        args.output = os.path.join("assets", stem + ".fft")

    samples, rate = read_wav_mono(args.input)
    peak = float(np.max(np.abs(samples)))
    if peak > 0:
        samples = samples / peak

    nyquist = rate / 2.0
    high = args.high if args.high is not None else nyquist
    if args.low < 0 or high > nyquist or args.low >= high:
        sys.exit(
            "invalid frequency range [%g, %g] Hz (Nyquist = %g Hz)"
            % (args.low, high, nyquist))

    hop = rate // args.fps
    win = args.window
    if win & (win - 1):
        sys.exit("--window must be a power of 2; got %d" % win)

    bin_hz = rate / float(win)
    low_bin = max(0, int(args.low / bin_hz))
    high_bin = min(win // 2 + 1, int(np.ceil(high / bin_hz)))
    if low_bin >= high_bin:
        sys.exit("resolved bin range [%d, %d) is empty; widen --low/--high "
                 "or increase --window" % (low_bin, high_bin))

    n_frames = (len(samples) + hop - 1) // hop
    pad = win // 2
    padded = np.concatenate([
        np.zeros(pad, dtype=np.float32),
        samples.astype(np.float32),
        np.zeros(pad + win, dtype=np.float32),
    ])
    window = np.hanning(win).astype(np.float32)

    energies = np.zeros(n_frames, dtype=np.float32)
    for i in range(n_frames):
        chunk = padded[i * hop: i * hop + win]
        spectrum = np.fft.rfft(chunk * window)
        energies[i] = float(np.sum(np.abs(spectrum[low_bin:high_bin])))

    e_peak = float(np.max(energies))
    if e_peak > 0:
        energies *= 255.0 / e_peak
    bytes_out = np.clip(np.round(energies), 0, 255).astype(np.uint8)

    out_dir = os.path.dirname(args.output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(bytes_out.tobytes())

    print("[wav2fft] %s" % args.input)
    print("  rate=%d Hz, length=%.2fs" % (rate, len(samples) / rate))
    print("  window=%d (%.2f Hz/bin), band=bins[%d..%d) = %.1f..%.1f Hz"
          % (win, bin_hz, low_bin, high_bin,
             low_bin * bin_hz, high_bin * bin_hz))
    print("  -> %s (%d bytes, %.2fs @ %d fps)"
          % (args.output, len(bytes_out), len(bytes_out) / args.fps, args.fps))


if __name__ == "__main__":
    main()
