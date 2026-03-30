#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Build first
make

# Launch DOSBox-X
if command -v dosbox-x &> /dev/null; then
    dosbox-x -conf dosbox-x.conf
else
    echo "ERROR: dosbox-x not found. Run ./setup.sh first." >&2
    exit 1
fi
