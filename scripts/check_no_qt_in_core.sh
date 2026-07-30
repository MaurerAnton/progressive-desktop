#!/bin/sh
# Fails if any src/core/ file includes a Qt header.
# Enforces the Qt-free core boundary for Android NDK + WASM portability.
set -e
matches=$(grep -rl '#include <Q\|#include "Q' src/core/ 2>/dev/null || true)
if [ -n "$matches" ]; then
    echo "FAIL: Qt header found in src/core/ — core must be Qt-free for portability:"
    echo "$matches"
    exit 1
fi
echo "OK: src/core/ is Qt-free"
