#!/usr/bin/env bash
# scripts/build.sh — build + optional test. AI-friendly one-liner.
# Usage: ./scripts/build.sh              # build only
#        ./scripts/build.sh test         # build + ctest
#        ./scripts/build.sh visual       # build + test_visual
#        ./scripts/build.sh all          # build + ctest + test_visual
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo ">> Building..."
cmake --build build -j4

case "${1:-}" in
    test|all)
        echo ">> Running ctest..."
        ctest --test-dir build
        ;;
esac

case "${1:-}" in
    visual|all)
        echo ">> Running test_visual..."
        QT_QPA_PLATFORM=offscreen ./build/test_visual
        ;;
esac

echo ">> OK"
