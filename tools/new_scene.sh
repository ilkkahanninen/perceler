#!/bin/bash
# Creates a new scene with boilerplate .c and .h files

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <scene_name>"
    echo "Example: $0 starfield"
    exit 1
fi

NAME="$1"
GUARD="$(echo "$NAME" | tr '[:lower:]' '[:upper:]')_H"
DIR="$(dirname "$0")/../src/scenes"

C_FILE="$DIR/${NAME}.c"
H_FILE="$DIR/${NAME}.h"

if [ -f "$C_FILE" ] || [ -f "$H_FILE" ]; then
    echo "Error: scene '$NAME' already exists"
    exit 1
fi

cat > "$H_FILE" << EOF
#ifndef ${GUARD}
#define ${GUARD}

#include <scene.h>

extern const Scene ${NAME}_scene;

#endif
EOF

cat > "$C_FILE" << EOF
#include "${NAME}.h"

#include <vga.h>

static void ${NAME}_setup(void) {}

static void ${NAME}_init(unsigned char *backbuffer) {
  (void)backbuffer;
}

static void ${NAME}_shutdown(void) {}

static void ${NAME}_render(unsigned char *backbuffer, unsigned int frame) {
  unsigned char *dst = backbuffer;
  int x, y;

  for (y = 0; y < VGA_HEIGHT; y++) {
    for (x = 0; x < VGA_WIDTH; x++) {
      *dst++ = 0;
    }
  }
  vga_vsync();
  vga_blit(backbuffer);
}

const Scene ${NAME}_scene = {${NAME}_setup, ${NAME}_init, ${NAME}_shutdown,
                             ${NAME}_render};
EOF

echo "Created $C_FILE and $H_FILE"
echo ""
echo "To use it, add to the timeline in src/demo.c:"
echo "  #include \"scenes/${NAME}.h\""
echo "  {&${NAME}_scene, XM_MS(BPM, SPEED, PATTERN_LEN * 4)},"
