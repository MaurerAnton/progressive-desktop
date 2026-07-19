# Phase 2 — Minimal Usable UI

## What's built

A working Matrix chat client GUI on top of the Phase 1 core:

| File | Purpose |
|---|---|
| `src/ui/login_dialog.{hpp,cpp}` | Modal login dialog. Calls `discoverHomeserver` + `loginWithPassword`. Persists session via `SessionStore`. |
| `src/ui/main_window.{hpp,cpp}` | Top-level window. Sidebar (room list) + timeline + message edit. Owns the `SyncEngine` and wires its callbacks to UI-thread slots via `QMetaObject::invokeMethod(Qt::QueuedConnection)`. |
| `src/ui/room_list_model.{hpp,cpp}` | `QAbstractListModel` for joined rooms. Updates incrementally on each /sync. Sorted by last activity (descending). |
| `src/ui/timeline_view.{hpp,cpp}` | `QTextBrowser` rendering messages as HTML via `progressive::markdownToHtml`. Handles `m.room.message` (text/emote/notice/image), `m.room.encrypted` (placeholder until Phase 4), `m.room.member` (joins/leaves), `m.room.redaction`. Deduplicates by event_id. |
| `src/ui/message_edit.{hpp,cpp}` | `QTextEdit` — Enter to send, Shift+Enter for newline. Routes `/cmd args` to `slashCommand` signal, plain text to `sendMessage` signal. |

## New MatrixClient methods

Added 3 endpoints to `src/core/matrix_client.{hpp,cpp}`:

- `sendMessage(roomId, body, msgtype="m.text")` → `POST /_matrix/client/v3/rooms/{roomId}/send/m.room.message/{txnId}`. Returns event_id.
- `getMessages(roomId, from="", limit=30)` → `GET /_matrix/client/v3/rooms/{roomId}/messages?dir=b`. Returns raw JSON.
- `setReadMarker(roomId, eventId)` → `POST /_matrix/client/v3/rooms/{roomId}/read_markers`.

## Slash commands

| Command | Action |
|---|---|
| `/help` | List commands in timeline |
| `/clear` | Clear timeline view |
| `/logout` | Stop sync, logout, clear session |
| `/me <action>` | Send as `m.emote` |
| Anything else | "unknown command" hint |

## Layout

```
┌──────────────┬───────────────────────────┐
│ Room list    │ Timeline                  │
│ (sidebar,    │ (markdown bubbles,        │
│  280-380px)  │  auto-scroll)             │
│              │                           │
│              ├───────────────────────────┤
│              │ MessageEdit (input)       │
└──────────────┴───────────────────────────┘
                Status bar: sync state + stats
```

`PROGRESSIVE_MOBILE=ON` (pinetab2 preset) → fullscreen frameless window.

## How to test on PineTab 2

### Build

```bash
cd progressive-desktop
git pull --recurse-submodules
./scripts/build-pt2.sh rebuild    # ccache warm → ~2 min
```

### Run GUI

```bash
./build/progressive-desktop
```

First launch: shows login dialog (server default = `matrix.org`, enter your Matrix user + password). After login, the main window opens and starts syncing.

Second launch onwards: detects saved session at `~/.local/share/progressive-desktop/session.db` and skips the login dialog.

### Verify each piece

| Test | Command | Expected |
|---|---|---|
| Link test | `./build/progressive-desktop --smoke` | All 4 components + markdown probe |
| Discovery | `./build/progressive-desktop --discover matrix.org` | 19 server versions + 3 login flows |
| Unit tests | `ctest --test-dir build --output-on-failure` | 5/5 pass |
| Login + sync (CLI) | `./build/progressive-desktop --login user pass && ./build/progressive-desktop --sync 3` | 3 syncs received |
| Login + sync (GUI) | `./build/progressive-desktop` | Login dialog → main window with rooms + timeline |

### Try the GUI

1. Launch — login dialog appears
2. Enter Matrix credentials — main window opens, sync starts
3. Rooms appear in the left sidebar within a few seconds (initial sync)
4. Click a room — timeline loads (last ~30 events from initial sync)
5. Type a message in the bottom input — Enter to send (local echo appears immediately)
6. Try `/help` to see slash commands

## Known limitations (Phase 3+)

- **E2EE**: encrypted rooms show `[encrypted message — decryption in Phase 4]` placeholder. Messages sent to encrypted rooms will fail (server returns `M_FORBIDDEN` since we don't encrypt). Use unencrypted DMs/rooms for Phase 2 testing.
- **No backward pagination**: only events from initial + incremental sync are shown. `/messages` endpoint is wired but UI doesn't trigger it yet.
- **No read receipts sending**: `setReadMarker` exists but isn't auto-called on room open.
- **No typing indicators, reactions, replies**: these come in Phase 3+.
- **No media**: image/video/audio/file messages show `[attachment]` placeholder.

## File map

```
src/
  main.cpp                   CLI + GUI entry
  core/
    http_client.{hpp,cpp}    libcurl wrapper (Phase 1)
    matrix_client.{hpp,cpp}  CS API: login/sync/send/messages/read_markers
    session_store.{hpp,cpp}  SQLite persistence (Phase 1)
    sync_engine.{hpp,cpp}    background /sync loop (Phase 1)
    account_info.hpp         shared AccountInfo struct
  ui/
    login_dialog.{hpp,cpp}   modal login
    main_window.{hpp,cpp}    top-level window + sync wiring
    room_list_model.{hpp,cpp} QAbstractListModel for rooms
    timeline_view.{hpp,cpp}  QTextBrowser chat view
    message_edit.{hpp,cpp}   input + slash commands
```
