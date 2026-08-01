# CI Build History & Failure Analysis

## Current Status: GREEN — two workflows pass on main

- **Build + Test** (`build.yml`) — configure (ci preset) → build → ctest, on every push/PR to main.
- **Synapse E2EE Integration** (`synapse-e2ee.yml`, added Aug 1) — starts a real
  `matrixdotorg/synapse` container at `:8008`, waits for `/versions`, builds, then runs
  `./build/test_synapse_e2ee` against it. Registers 2 users → creates encrypted room →
  shares room key → cross-account decrypt. Verified GREEN on `031dc04`.

## Synapse-E2EE workflow gotchas (why it took 6 iterations)

1. **`chown` breaks the container**: Synapse runs as uid 991 and needs to read its signing
   key. `sudo chown runner` on `/data` → container dies. Revert to `sudo tee -a`/`sudo sed -i`
   and `chown 991:991` only the config file.
2. **Duplicate YAML keys are NOT honored**: appending `enable_registration: true` to a config
   that already has `enable_registration: false` keeps `false` (PyYAML keeps the first). Use
   `sed`-style in-place replacement (`python3 -c` regex replace, not heredoc — heredocs break
   the workflow YAML's block scalar indentation).
3. **`grep` on a missing key fails the step** (`set -e`): verify with `|| true`.
4. Container runs with `-v "$PWD/synapse-data:/data"`; generation step must stay on the runner
   (not inside the container) so the config edit happens before `start`.

## Failed Commits

### `842b9cb` (thread root snapshot) — COMPILE ERROR
- **Error**: `hasRootSnapshot` / `rootSnapshot` not captured in inner lambda
- **Root cause**: Added `rootSnapshot` + `hasRootSnapshot` to outer lambda capture
  but forgot the inner `QMetaObject::invokeMethod` lambda
- **Fix**: `f24961a` added the captures to the inner lambda
- **Local build passed because**: fix was in working tree but not staged

### `f24961a` (inner lambda fix) — expected GREEN
- Added `rootSnapshot` + `hasRootSnapshot` to inner lambda + `#include "core/debug_log.hpp"`

### `841a710` (thread flags + use-after-move) — LINK ERROR
- **Error**: `undefined symbol: progressive::desktop::extractThreadRootId`
- **Root cause**: Declared `extractThreadRootId` in `room_store.hpp` but the
  implementation in `room_store.cpp` was still named `threadRootId` (no rename)
- **Fix**: `b49d554` renamed `threadRootId` -> `extractThreadRootId` in .cpp

### `ad53681`, `2a95fb7` — expected GREEN
- Simple UI edits, no new symbols

## CI Recommendations

1. **Build locally with the CI preset before pushing**:
   ```bash
   cmake --preset ci && cmake --build build -j$(nproc) && ctest --test-dir build
   ```
   The CI preset (`CMakePresets.json` line 36) uses Release mode, no ccache, LTO off.

2. **Never commit from a dirty working tree**: Several failures happened because
   local fixes were applied to the working tree after `git add` but before
   `git commit`. Use `git diff --cached` to verify exactly what's staged
   before committing. Or use `git commit -a` only if all tree changes are
   intended.

3. **Add a `verify-build.sh` pre-push hook**:
   ```bash
   # .git/hooks/pre-push
   #!/bin/bash
   set -e
   cmake --preset ci 2>&1 | tail -1
   cmake --build build -j4 2>&1 | tail -3
   ctest --test-dir build 2>&1 | tail -3
   ```

4. **Run full `./scripts/build.sh all` before pushing**: The fast incremental
   build (`ninja -C build`) can succeed even with link errors if the changed
   .cpp is in a library that hasn't been re-linked. Always do a full link step.

## PineTab Build Notes

- PineTab uses `cmake --preset pinetab2` (Debug, ccache, LTO off, mobile OFF)
- CI uses `cmake --preset ci` (Release, no ccache, LTO off, mobile OFF)
- Key difference: Debug vs Release optimization levels. Some UB/use-after-move
  bugs may only manifest in Debug (crashes) or Release (silent corruption).
  Always test both.
