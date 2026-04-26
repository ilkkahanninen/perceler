#!/bin/bash
# Disassemble a C source file and open the annotated result in VS Code.
#
# Usage: tools/wdis.sh <src/path/to/file.c>
#
# Builds a one-off object with debug info, runs `wdis -a` with source
# lines interleaved as comments, writes build/<name>.disasm and opens
# it in the currently focused VS Code editor column.

set -e

SRC="$1"
if [ -z "$SRC" ]; then
  echo "Usage: $0 <source.c>" >&2
  exit 1
fi

if [ ! -f "$SRC" ]; then
  echo "Source not found: $SRC" >&2
  exit 1
fi

BASE="$(basename "$SRC" .c)"
OBJ_DBG="build/${BASE}.dbg.obj"
OUT="build/${BASE}.disasm"
WATCOM_BIN="tools/watcom/armo64"

# Same CFLAGS as the Makefile, but with -d1 (line-number debug info).
CFLAGS="-5 -fpi87 -fp3 -d1 -otexan -zp4 -oa -mf -bt=dos \
  -i=tools/watcom/h \
  -i=libs/libxmp-lite/include/libxmp-lite \
  -i=libs/libxmp-lite/src \
  -i=src \
  -i=src/engine \
  -DLIBXMP_CORE_PLAYER -DLIBXMP_NO_PROWIZARD"

# Make sure the regular object file is up to date (asset header depends on it).
make --silent build/demo.dat

# Compile with debug info.
"$WATCOM_BIN/wcc386" $CFLAGS "-fo=$OBJ_DBG" "$SRC" > /dev/null

# -a  : assembleable output (no address/byte columns)
# -fi : use [base+disp] addressing instead of the old disp[base] notation
# -s= : interleave source lines as comments
"$WATCOM_BIN/wdis" -a -fi "-s=$SRC" "$OBJ_DBG" > "$OUT"

if command -v code >/dev/null 2>&1; then
  code -r "$OUT"
else
  echo "Disassembly written to $OUT" >&2
  echo "(Install VS Code's 'code' CLI for automatic opening.)" >&2
fi
