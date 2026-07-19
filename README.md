# Progressive Chat — Desktop

A Matrix client in the making. The goal: a truly progressive messenger with a **pure C++ native** core.

Desktop sister to [`progressive-android-next`](https://github.com/MaurerAnton/progressive-android-next). Built with Qt6 QWidgets, libolm, and the shared `progressive_native` C++ core. For Linux desktop and PineTab 2 / PinePhone.

**Website:** [progressive.chat](https://progressive.chat)

## Vision

- **Pure C++ core** — no Electron, no JVM, no Rust SDK.
- **One shared core** with the Android side — `progressive_native` is built from the same source.
- **Clean, snappy UI** that respects your attention.
- **Full Matrix compatibility** — no compromises on federation.
- **Open source** — AGPLv3.

## Status

Phase 0 — toolchain scaffolding. The app opens a Qt6 window and links against `progressive_native`. No Matrix functionality yet.

See [`ROADMAP.md`](ROADMAP.md) (planned) and the [Phase 0 audit](docs/audit.md) for which `progressive_native` modules are real vs stubs.

## Build

### Requirements (PineTab 2 / DanctNIX)

```bash
sudo pacman -S base-devel cmake ninja ccache git \
    qt6-base qt6-tools qt6-declarative qt6-multimedia \
    qt6-svg qt6-wayland curl openssl
```

### Configure + build

```bash
git clone --recurse-submodules https://github.com/MaurerAnton/progressive-desktop.git
cd progressive-desktop
./scripts/build-pt2.sh         # or: cmake --preset pinetab2 && cmake --build build -j4
./build/progressive-desktop
```

### Other presets

```bash
cmake --preset desktop         # Linux desktop, release, LTO on
cmake --preset ci              # CI
```

## Module audit

`progressive_native` is built from the [`progressive-android-experiments`](https://github.com/MaurerAnton/progressive-android-experiments) submodule. Of the 889 `.cpp` files, not all are real implementations. Run the audit:

```bash
./scripts/audit_modules.py            # summary
./scripts/audit_modules.py --verbose  # per-file
./scripts/audit_modules.py --csv      # CSV
./scripts/audit_modules.py --tsv      # TSV (used by CMake at configure time)
```

Tiers:
- **A** — real hand-written implementations (used directly)
- **B** — auto-generated templates (echo JSON back; need real impl upstream)
- **C** — pure stubs (hash/size echo; need real impl upstream)
- **D** — Android JNI glue (excluded from desktop build)

## Architecture

```
progressive-desktop/
  CMakeLists.txt          top-level
  CMakePresets.json       pinetab2 / desktop / ci
  cmake/
    progressive_native.cmake    builds the shared C++ core
  third_party/
    progressive-android-experiments/   git submodule (sparse: progressive/src/main/cpp/)
    android_shim/                    <android/log.h> shim for desktop
  src/
    main.cpp              Phase 0 stub window
  scripts/
    build-pt2.sh          PineTab 2 build wrapper (ccache + ninja)
    audit-modules.py      Tier A/B/C/D classifier
```

## License

AGPLv3. See [`LICENSE`](LICENSE).
