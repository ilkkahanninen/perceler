#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/tools"
WATCOM_DIR="$TOOLS_DIR/watcom"

OW2_URL="https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz"

# Detect platform and set OW2 host binary directory
detect_platform() {
    local os arch
    os="$(uname -s)"
    arch="$(uname -m)"

    case "$os" in
        Darwin)
            case "$arch" in
                arm64)  echo "armo64" ;;
                x86_64) echo "osx64"  ;;
                *)      echo "ERROR: Unsupported macOS arch: $arch" >&2; exit 1 ;;
            esac
            ;;
        Linux)
            case "$arch" in
                x86_64)  echo "binl64" ;;
                i*86)    echo "binl"   ;;
                aarch64) echo "arml64" ;;
                *)       echo "ERROR: Unsupported Linux arch: $arch" >&2; exit 1 ;;
            esac
            ;;
        *)
            echo "ERROR: Unsupported OS: $os" >&2; exit 1
            ;;
    esac
}

HOST_BINDIR="$(detect_platform)"
echo "Platform: $(uname -s) $(uname -m) -> OW2 bindir: $HOST_BINDIR"

# --- Install Open Watcom V2 ---
if [ -x "$WATCOM_DIR/$HOST_BINDIR/wcc386" ]; then
    echo "Open Watcom V2 already installed in $WATCOM_DIR"
else
    echo "Downloading Open Watcom V2..."
    mkdir -p "$TOOLS_DIR"
    ARCHIVE="$TOOLS_DIR/ow-snapshot.tar.xz"

    if [ ! -f "$ARCHIVE" ]; then
        curl -L --progress-bar -o "$ARCHIVE" "$OW2_URL"
    fi

    echo "Extracting Open Watcom V2..."
    mkdir -p "$WATCOM_DIR"
    tar -xf "$ARCHIVE" -C "$WATCOM_DIR" --strip-components=1

    # Make host binaries executable
    chmod +x "$WATCOM_DIR/$HOST_BINDIR"/*

    echo "Open Watcom V2 installed."

    # Clean up archive to save space
    rm -f "$ARCHIVE"
fi

# Verify OW2
export WATCOM="$WATCOM_DIR"
export PATH="$WATCOM/$HOST_BINDIR:$PATH"

echo -n "Verifying wcc386... "
WCC_OUT=$("$WATCOM/$HOST_BINDIR/wcc386" 2>&1 || true)
if echo "$WCC_OUT" | grep -qi "watcom"; then
    echo "OK"
else
    echo "FAILED"
    echo "wcc386 not working. Check installation." >&2
    exit 1
fi

echo -n "Verifying wlink... "
WLINK_OUT=$("$WATCOM/$HOST_BINDIR/wlink" 2>&1 || true)
if echo "$WLINK_OUT" | grep -qi "watcom"; then
    echo "OK"
else
    echo "FAILED"
    exit 1
fi

# Verify DOS/32A is present
if [ -f "$WATCOM_DIR/binw/dos32a.exe" ]; then
    echo "DOS/32A extender: OK (bundled with OW2)"
else
    echo "WARNING: dos32a.exe not found in $WATCOM_DIR/binw/"
fi

# --- Install DOSBox-X ---
if command -v dosbox-x &> /dev/null; then
    echo "DOSBox-X already installed: $(which dosbox-x)"
else
    case "$(uname -s)" in
        Darwin)
            if command -v brew &> /dev/null; then
                echo "Installing DOSBox-X via Homebrew..."
                brew install dosbox-x
            else
                echo "WARNING: Homebrew not found. Install DOSBox-X manually:"
                echo "  https://github.com/joncampbell123/dosbox-x/releases"
            fi
            ;;
        Linux)
            echo "WARNING: Please install DOSBox-X for your distribution:"
            echo "  https://github.com/joncampbell123/dosbox-x/releases"
            echo "  Or: sudo apt install dosbox-x / sudo dnf install dosbox-x"
            ;;
    esac
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "To build:  make"
echo "To run:    ./run.sh"
