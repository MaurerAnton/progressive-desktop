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

Phase 2 — minimal usable UI. The app logs in, syncs, and renders a working
chat client:

- Login dialog with homeserver discovery + password login
- Room list sidebar (avatars, unread badges, invites with accept/reject)
- Timeline with chat bubbles, avatars, sender names, grouping, timestamps
- Markdown body rendering via `progressive::markdownToHtml`
- Reactions, replies (with preview), threads, pinned messages
- Image / video / file / audio attachments, image viewer dialog
- Slash commands (`/help`, `/clear`, `/logout`, `/me`)
- Emoji picker, profile / room settings / room members / room directory dialogs
- SQLite-backed session persistence (WAL + `synchronous=FULL`)
- Background `/sync` loop with exponential backoff

**Not yet:** E2EE (encrypted rooms show a placeholder), backward pagination UI,
read-receipt auto-send, typing indicators. See [`docs/phase2.md`](docs/phase2.md)
"Known limitations" for the full list.

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

## CLI subcommands

The binary also runs headless for testing:

```bash
./build/progressive-desktop --smoke                # link + markdown probe
./build/progressive-desktop --discover matrix.org  # server discovery + versions + login flows
./build/progressive-desktop --login <user> <pass>  # login + persist session
./build/progressive-desktop --sync <n>             # do N syncs then stop
./build/progressive-desktop --memcheck             # struct-size + memory snapshots
```

## Module audit

`progressive_native` is built from the [`progressive-android-experiments`](https://github.com/MaurerAnton/progressive-android-experiments) submodule. Of the 878 `.cpp` files, not all are real implementations; Tier filtering drops 283 stubs + 8 JNI files — **595 sources** actually compiled. Run the audit:

```bash
./scripts/audit_modules.py            # summary
./scripts/audit_modules.py --verbose  # per-file
./scripts/audit_modules.py --csv      # CSV
./scripts/audit_modules.py --tsv      # TSV (used by CMake at configure time)
```

Tiers:
- **A** — real hand-written implementations (used directly)
- **B** — auto-generated templates (echo JSON back; need real impl upstream)
- **C** — pure stubs (hash/size echo; excluded from desktop build)
- **D** — Android JNI glue (excluded from desktop build)

## Architecture

```
progressive-desktop/
  CMakeLists.txt          top-level
  CMakePresets.json       pinetab2 / desktop / ci
  cmake/
    progressive_native.cmake    builds the shared C++ core (Tier filter)
  third_party/
    progressive-android-experiments/   git submodule (sparse: progressive/src/main/cpp/)
    android_shim/                    <android/log.h> + STL compat shims for desktop
  src/
    main.cpp              Phase 2 entry: CLI subcommands + GUI mode
    core/
      http_client.{hpp,cpp}      libcurl wrapper (TLS, SOCKS5, proxy)
      matrix_client.{hpp,cpp}   CS API: login / sync / send / messages / read_markers
      session_store.{hpp,cpp}   SQLite persistence (WAL + checkpoint)
      sync_engine.{hpp,cpp}     background /sync loop with backoff
      fast_sync.{hpp,cpp}       incremental sync parser
      memory_stats.{hpp,cpp}    struct-size + snapshot diagnostics
      notifications.{hpp,cpp}   desktop notifications
      crypto/                  libolm wrappers (Phase 4)
    ui/
      main_window.{hpp,cpp}         top-level window + sync wiring
      login_dialog.{hpp,cpp}        modal login
      chat_view.{hpp,cpp}           message sending / file attach logic
      room_list_model.{hpp,cpp}     QAbstractListModel for rooms
      room_list_delegate.{hpp,cpp}  paints room list rows
      timeline_model.{hpp,cpp}       QAbstractListModel for events
      timeline_delegate.{hpp,cpp}    paints chat bubbles
      timeline_handlers.{hpp,cpp}    context-menu actions (react/edit/delete/pin)
      message_edit.{hpp,cpp}         input + slash commands
      emoji_picker.{hpp,cpp}         emoji picker
      image_loader.{hpp,cpp}         async mxc:// thumbnail fetcher
      image_viewer_dialog.{hpp,cpp}  full-size image viewer
      prefs_dialog / profile_dialog / room_directory_dialog /
      room_members_dialog / room_settings_dialog / threads_dialog /
      user_profile_dialog / network_log_dialog
      theme.{hpp,cpp}                dark palette + design tokens
  scripts/
    build-pt2.sh          PineTab 2 build wrapper (ccache + ninja)
    audit-modules.py      Tier A/B/C/D classifier
  docs/
    phase1.md            Phase 1 — core plumbing
    phase2.md            Phase 2 — minimal usable UI
  tests/
    test_phase1.cpp       unit tests (no network)
```

## License

AGPLv3. See [`LICENSE`](LICENSE).
