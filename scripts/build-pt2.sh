#!/usr/bin/env bash
# scripts/build-pt2.sh — PineTab 2 build wrapper
# Configures (if needed) and builds progressive-desktop natively on PineTab 2.
#
# Usage:
#   ./scripts/build-pt2.sh            # configure + build
#   ./scripts/build-pt2.sh rebuild    # wipe build/ and reconfigure
#   ./scripts/build-pt2.sh run        # build then run the binary
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
JOBS="${JOBS:-4}"

# Ensure ccache is set up
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.ccache}"
mkdir -p "$CCACHE_DIR"
ccache -M 10G >/dev/null 2>&1 || true

# Wipe and reconfigure if requested
if [[ "${1:-}" == "rebuild" ]]; then
    echo ">> Wiping $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# Configure if not already done
if [[ ! -d "$BUILD_DIR" || ! -f "$BUILD_DIR/build.ninja" ]]; then
    echo ">> Configuring (preset: pinetab2)"
    cmake --preset pinetab2
else
    echo ">> Build dir exists, skipping configure"
fi

# Build
echo ">> Building with -j$JOBS"
cmake --build "$BUILD_DIR" -j"$JOBS"

BINARY="$BUILD_DIR/progressive-desktop"
if [[ ! -f "$BINARY" ]]; then
    echo "!! Build finished but $BINARY not found"
    exit 1
fi

echo ">> OK: $BINARY"

# Run if requested
if [[ "${1:-}" == "run" ]]; then
    echo ">> Running"
    exec "$BINARY"
fi
