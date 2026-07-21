#!/usr/bin/env bash
# scripts/pull.sh — safe git pull with submodule cleanup.
# Patches from CMake configure step modify submodule files.
# This script cleans them before pulling to avoid "overwritten by checkout" errors.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo ">> Cleaning submodule changes..."
cd third_party/progressive-android-experiments
git checkout -- .
cd "$ROOT"

echo ">> Pulling..."
git pull --recurse-submodules

echo ">> Done. To rebuild: ./scripts/build-pt2.sh rebuild"
