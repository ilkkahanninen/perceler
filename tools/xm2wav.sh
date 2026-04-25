#!/usr/bin/env bash
# Convert an XM module to a WAV file using the xmp CLI tool.
#
# Usage:
#   tools/xm2wav.sh input.xm [output.wav] [rate]
#
# Defaults:
#   output = <input stem>.wav, next to the input file
#   rate   = 44100

set -euo pipefail

if ! command -v xmp >/dev/null 2>&1; then
    echo "xmp not found on PATH." >&2
    echo "Install it (re-run ./setup.sh, or):" >&2
    echo "  macOS: brew install xmp" >&2
    echo "  Linux: sudo apt install xmp / sudo dnf install xmp" >&2
    exit 1
fi

if [ "$#" -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo "Usage: $0 input.xm [output.wav] [rate]" >&2
    exit 1
fi

INPUT="$1"
OUTPUT="${2:-${INPUT%.[Xx][Mm]}.wav}"
RATE="${3:-44100}"

if [ ! -f "$INPUT" ]; then
    echo "Input file not found: $INPUT" >&2
    exit 1
fi

echo "[xm2wav] $INPUT -> $OUTPUT  (${RATE} Hz)"
xmp --nocmd -d wav -o "$OUTPUT" --frequency "$RATE" "$INPUT"
echo "[xm2wav] done."
