# Progressive Chat — Desktop

A Matrix client in the making. Built with Qt6 QWidgets, libolm, and a shared `progressive_native` C++ core. For Linux desktop and PineTab 2 / PinePhone.

**Website:** [progressive.chat](https://progressive.chat)

## Vision

- **Pure C++ core** — no Electron, no JVM, no Rust SDK.
- **Shared core with the Android side** — `progressive_native` is built from the
  `progressive-android-experiments` submodule source. Honest scope (verified Aug 4):
  what's genuinely shared is the **Tier A/B utility layer** (JSON parsing, auth/account
  models, sync models, markdown, login flow, `well_known`). The desktop's HTTP/TLS,
  sync engine, session store, and **all E2EE crypto are hand-written desktop code**
  (libcurl-vs-JNI, verified E2EE stack) — NOT the Android repo's crypto, much of which
  is stub/boilerplate. The mobile-frontend plan (reuse desktop core via X1; Kotlin/JNI
  vs Qt-for-Android — undecided) lives in `memory/DREAM.md`.
- **Clean, snappy UI** that respects your attention.
- **Full Matrix compatibility** — no compromises on federation.
- **Open source** — AGPLv3.

## Status

**⚠️ NOT USABLE for daily use. Under active development. Do not rely on it.**

E2EE works for a basic 2-user, 1-device flow (verified in CI against a live Synapse),
and the CI suite also exercises a 3-member multi-account/multi-device scenario
(alice on 2 devices, late joiner, room-key delivery). Cross-signing device trust is
in progress (Phase 6 tail); SSSS key backup is Phase 7. Several critical bugs remain
(thread replies, image rendering).

Current phase: E2EE (cross-signing) + bug fixing toward v0.5 (see `memory/PROGRESS.md`
for the live tracker).

CI test coverage today: 2 users × 1 device each, plus a multi-account/multi-device
scenario (3 members, alice on 2 devices). Untested: full device-management UI flows.

What works:
- Login/logout with homeserver discovery + password login
- Room list sidebar (avatars, unread badges, invites with accept/reject)
- Timeline renders `m.room.message` events with chat bubbles + avatars + grouping
- Background `/sync` loop with exponential backoff + token refresh
- SQLite-backed session persistence (account, sync token, crypto pickles)
- Slash commands (`/invite`, `/leave`, `/kick`, `/ban`, `/join`, `/me`)
- Emoji picker, quick-react on last message
- File/image/audio attachment upload
- Threads: view, reply, indicator (`💬 N replies`)
- Reactions: context-menu add/remove
- Load-more (backward pagination)
- Multi-account (switch between saved accounts)
- Account switcher dropdown in toolbar
- E2EE: outbound encryption (Megolm) + inbound decryption (Olm/Megolm recovery chain)
- E2EE: cross-account key sharing (2 users, 1 device each) — verified in CI against a
  live Synapse server (`test_synapse_e2ee.cpp`). Multi-account / multi-device scenario
  (3 members, alice on 2 devices) also exercised in CI.
- E2EE: SAS device verification (m.sas.v1) — 7-emoji match dialog, device verification
- E2EE: cross-signing (Phase 6) — key upload/signatures + device trust shields (in progress)
- Ctrl+K room switcher, date dividers, keyboard navigation

Not yet implemented:
- Read receipts, typing indicators (sending)
- Cross-signing device trust chain — full (partially in, Phase 6 tail)
- SSSS key backup / history recovery after re-login

## Build

### Requirements (PineTab 2 / DanctNIX, Arch)

```bash
sudo pacman -S base-devel cmake ninja ccache mold git \
    qt6-base qt6-tools qt6-declarative qt6-multimedia \
    qt6-svg qt6-wayland curl openssl libsodium python
```

> **mold** is required — the `pinetab2` preset links with `-fuse-ld=mold` (fast linker).
> **libsodium** is required — ed25519/cross-signing crypto (Phases 6-7).
> **python** is required — `cmake/progressive_native.cmake` runs `audit_modules.py` at
> configure time to classify submodule sources.

### Requirements (Linux desktop, Debian/Ubuntu)

```bash
sudo apt-get install -y qt6-base-dev libqt6widgets6 qt6-base-dev-tools \
  cmake ninja-build ccache mold libcurl4-openssl-dev libsqlite3-dev libsodium-dev \
  libolm-dev python3 git
```

### Dependencies fetched automatically (network needed at configure time)

- **libolm** (E2EE) — `FetchContent` from gitlab.matrix.org (`progressive_native.cmake:108`)
- **simdjson** (fast JSON) — `FetchContent` (`progressive_native.cmake:120`)
- **SQLite3 amalgamation** — downloaded from sqlite.org into the build dir (`progressive_native.cmake:137`)

### Configure + build

```bash
git clone --recurse-submodules https://github.com/MaurerAnton/progressive-desktop.git
cd progressive-desktop
./scripts/build-pt2.sh         # or: cmake --preset pinetab2 && cmake --build build -j4
./build/progressive-desktop
```

> **Submodules are required.** `third_party/progressive-android-experiments` holds the
> shared `progressive_native` core. If you cloned without `--recurse-submodules`:
>
> ```bash
> git submodule update --init --recursive
> ```
>
> **After every `git pull`:** `git pull` does NOT update submodules. If a pull changed the
> submodule pointer, sync it or you'll get compile errors (e.g. missing `forgetOldFallbackKey`):
>
> ```bash
> git submodule update --init --recursive
> ```
>
> Daily build shortcut (PineTab): `git pull && git submodule update --init --recursive &&
> cmake --preset pinetab2 && cmake --build build -j4 && ./build/progressive-desktop`

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

`progressive_native` is built from the [`progressive-android-experiments`](https://github.com/MaurerAnton/progressive-android-experiments) submodule. Of the 889 `.cpp` files, not all are real implementations; Tier filtering keeps 521 A + 36 B = **557 sources** actually compiled (drops 324 C stubs + 8 D JNI files). Run the audit:

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
    main.cpp              entry: CLI subcommands + GUI mode
    core/
      http_client.{hpp,cpp}      libcurl wrapper (TLS, SOCKS5, proxy)
      matrix_client.{hpp,cpp}   CS API: login / sync / send / messages
      session_store.{hpp,cpp}   SQLite persistence (WAL + checkpoint)
      sync_engine.{hpp,cpp}     background /sync loop with backoff
      fast_sync.{hpp,cpp}       incremental sync parser (simdjson)
      memory_stats.{hpp,cpp}    struct-size + snapshot diagnostics
      thread_pool.{hpp,cpp}     global worker thread pool
      account_info.hpp          userId/deviceId/token struct
      crypto/                   libolm wrappers
        decryptor.{hpp,cpp}     E2EE coordinator (Olm + Megolm)
        olm_account.{hpp,cpp}   Olm account identity keys + pickling
        megolm_store.{hpp,cpp}  Megolm inbound session manager
    ui/
      main_window.{hpp,cpp}        top-level window + sync wiring
      ui_layout_builder.{hpp,cpp}  widget/layout creation for main window
      room_list_model.{hpp,cpp}    QAbstractListModel for rooms
      room_list_delegate.{hpp,cpp} paints room list rows
      chat/                        message input + sending
        chat_view.{hpp,cpp}        send message / file attach / quick-react
        message_edit.{hpp,cpp}     input box + @mention autocomplete
        emoji_picker.{hpp,cpp}     emoji picker dialog
      handlers/                    business logic (orchestration)
        auth_handler.{hpp,cpp}     login/logout/forceReLogin
        toolbar_handler.{hpp,cpp}  toolbar actions factory
        room_handler.{hpp,cpp}     room switch + invites + load-more
        thread_handler.{hpp,cpp}   thread view + reply
        room_context_menu.{hpp,cpp} context menus (leave/reply/react/pin)
        sync_response_handler.{hpp,cpp} sync → UI bridge
        account_switcher.{hpp,cpp} multi-account switch
        e2ee_init_handler.{hpp,cpp} E2EE init at startup
        session_bootstrap.{hpp,cpp} post-login startup sequence
        attachment_handler.{hpp,cpp} file download + image viewer
        slash_command_handler.{hpp,cpp} /invite, /leave, /kick, /ban, /join
      timeline/                    message display
        timeline_model.{hpp,cpp}   QAbstractListModel + seen-id dedup
        timeline_delegate.{hpp,cpp} editorEvent (clicks: react, thread, image)
        timeline_painter.{hpp,cpp} paint bubbles, avatars, reactions
        timeline_layout.{hpp,cpp}  bubble layout computation
        timeline_handlers.{hpp,cpp} react/edit/delete/pin server calls
      room/                        room data + sync processing
        room_store.{hpp,cpp}       sync → room list + timeline updates
        room_data_loader.{hpp,cpp} async history + member loading
      dialogs/                     modal dialogs
        login_dialog.{hpp,cpp}     login/register
        room_settings_dialog.{hpp,cpp}
        room_directory_dialog.{hpp,cpp}
        room_members_dialog.{hpp,cpp}
        threads_dialog.{hpp,cpp}
        image_viewer_dialog.{hpp,cpp}
        prefs_dialog.{hpp,cpp}
      profile/
        user_profile_dialog.{hpp,cpp}
      shared/
        theme.{hpp,cpp}            dark palette + design tokens
        image_loader.{hpp,cpp}     async mxc:// thumbnail fetcher
        notifications.{hpp,cpp}    desktop notifications (D-Bus)
  scripts/
    build-pt2.sh          PineTab 2 build wrapper (ccache + ninja)
    audit_modules.py      Tier A/B/C/D classifier
  memory/                 planning + tracking (not shipped)
  docs/
  tests/
    test_phase1.cpp       unit tests (no network)
    test_phase4.cpp       E2EE crypto tests
    test_gui_phase4.cpp   GUI + E2EE integration
    test_visual.cpp       widget/model smoke tests
```

## License

AGPLv3. See [`LICENSE`](LICENSE).
