# Phase 1 — Core Plumbing

## What's built

Four modules under `src/core/`:

| File | Purpose | Notes |
|---|---|---|
| `http_client.{hpp,cpp}` | libcurl-based HTTP client | Replaces JNI-based `http_client.cpp` in progressive_native (which delegates TLS to Android's `javax.net.ssl.SSLSocket` via `tls_bridge.cpp`). Supports TLS, SOCKS5 (Tor/I2P), HTTP proxy, redirects, timeouts. |
| `matrix_client.{hpp,cpp}` | Matrix Client-Server API client | Wraps HTTP client + delegates JSON parsing to progressive_native's Tier-A modules: `login_flow`, `well_known`, `matrix_error`, `sync_models`, `auth_models`, `json_parser`. |
| `session_store.{hpp,cpp}` | SQLite-backed persistence | Saves account + sync token to `~/.local/share/progressive-desktop/session.db`. WAL mode + `synchronous=FULL` + explicit `wal_checkpoint_v2(TRUNCATE)` after every save — same durability recipe as agora-desktop. |
| `sync_engine.{hpp,cpp}` | Background `/sync` loop | Dedicated worker thread. Exponential backoff (1s→60s capped). Persists since-token after each successful sync. Emits sync + state-change callbacks. |

## What's tested

`tests/test_phase1.cpp` (no Qt, no network needed):

- `progressive::parseServerDiscovery` (well_known.cpp)
- `progressive::parseLoginFlows` (login_flow.cpp)
- `progressive::parseMatrixErrorJson` (matrix_error.cpp)
- `progressive::parseSyncResponse` (sync_models.cpp)
- `SessionStore` round-trip on `:memory:` SQLite

## Build fixes (Phase 0 follow-ups)

Two issues discovered while building Phase 1:

1. **Missing `<algorithm>` and friends.** Android NDK's `<string>` transitively
   includes `<algorithm>`, `<cctype>`, `<numeric>`, etc. Strict gcc 16 on
   DanctNIX does not. Many Tier-A modules use `std::remove`, `std::find`,
   `std::sort` without an explicit `#include <algorithm>`.
   **Fix:** `third_party/android_shim/progressive_compat.h` is force-included
   via `-include` on every progressive_native TU. One header, no source patches
   to the read-only submodule.

2. **Broken stub headers.** ~50 Tier-C stubs have garbage headers like
   `std::string int(...)` or `std::string // or https(...)` — comments in the
   middle of declarations, keywords as function names. Other modules that
   `#include` these headers fail to compile.
   **Fix:** CMake now runs `scripts/audit_modules.py --tsv` at configure time
   and only includes Tier-A and Tier-B sources. Tier-C/D files are excluded
   entirely (they're stubs, contribute nothing).
   **Result:** 878 → 595 sources. 283 stubs + 8 JNI files dropped.

## How to test on PineTab 2

### 1. Build

```bash
cd progressive-desktop
git pull --recurse-submodules
./scripts/build-pt2.sh rebuild    # wipes build/ + reconfigures + builds
```

First build: ~30 min (595 sources × 4× A55). Subsequent incremental: ~30 sec
via ccache.

### 2. Smoke test (link check)

```bash
./build/progressive-desktop --smoke
```

Expected:
```
=== progressive-desktop Phase 1 smoke test ===
  http_client   : OK (libcurl 8.x.x ...)
  matrix_client  : OK (constructed)
  session_store : OK (opened :memory:)
  sync_engine    : OK (constructed)
  progressive_native::markdownToHtml("**hi**") = <strong>hi</strong>
All Phase 1 components linked successfully.
```

### 3. Unit tests

```bash
ctest --test-dir build --output-on-failure
```

Expected: 5 tests pass.

### 4. Live homeserver test (no login)

```bash
./build/progressive-desktop --discover matrix.org
```

Expected:
```
discovered homeserver: https://matrix.org
versions: {"versions":[...]}
login flows: ok (got 2 flows)
```

### 5. Login + sync test (needs real Matrix account)

```bash
./build/progressive-desktop --login youruser yourpass
./build/progressive-desktop --sync 3       # do 3 syncs then stop
```

### 6. GUI smoke (no Matrix)

```bash
./build/progressive-desktop
```

Opens a Qt6 window showing the markdown probe + libcurl version. No Matrix
functionality in the GUI yet — that's Phase 2.

## What we DON'T have yet (Phase 2+)

- E2EE (libolm wrapper for desktop — `olm_wrapper.cpp`)
- Real UI (room list, timeline, message edit)
- `/agent` AI system (Phase 3)
- Tor/I2P/Yggdrasil wiring (Phase 4)
- Spaces, threads, reactions UI (after Phase 5)

## File map

```
src/
  main.cpp                       CLI + GUI smoke
  core/
    http_client.{hpp,cpp}        libcurl wrapper
    matrix_client.{hpp,cpp}      CS API client
    session_store.{hpp,cpp}      SQLite persistence
    sync_engine.{hpp,cpp}        background /sync loop
tests/
  test_phase1.cpp                unit tests (no network)
cmake/
  progressive_native.cmake       builds the shared core (now with Tier filter)
third_party/
  android_shim/
    android/log.h                fprintf(stderr) shim for <android/log.h>
    progressive_compat.h         force-included STL headers
```
