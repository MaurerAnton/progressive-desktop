# AGENTS.md — AI Coding Rules for Progressive Chat

**Every AI agent MUST read this file AND code_map.json before making any code change.**
**Also read DREAM.md — it explains WHY the architecture exists.**

---

## Before You Touch Anything

1. **READ the file you plan to edit** — never edit a file you haven't read
2. **READ neighboring files** — understand patterns before copying them
3. **Check includes** — new headers may cause mass recompilation
4. **Read code_map.json** — find which files handle your feature before grepping

---

## Build & Verify

### Fast verification (after EVERY change — do not skip)

```bash
# Compile ONE file to .o — 2-5 sec (syntax + types + templates, no linker)
ninja -C build CMakeFiles/progressive-desktop.dir/src/ui/<path>/<file>.cpp.o

# Faster: -fsyntax-only — 1-3 sec (syntax + types, no codegen)
# Use ABSOLUTE path from project root, e.g.:
g++ -fsyntax-only -std=c++20 -Isrc -Ibuild/generated \
    $(pkg-config --cflags Qt6Widgets Qt6Network) \
    -include third_party/android_shim/progressive_compat.h \
    $PWD/src/ui/handlers/room_handler.cpp
```

Example: you edited `src/ui/handlers/room_handler.cpp`:
```bash
ninja -C build CMakeFiles/progressive-desktop.dir/src/ui/handlers/room_handler.cpp.o
```

### Compile only progressive_core (if core/*.cpp changed — no Qt, fast)
```bash
ninja -C build progressive_core
```

### Full build (before pushing — once at end of session)
```bash
./scripts/build.sh             # PineTab 2: build only (4 cores)
./scripts/build.sh all         # build + ctest + test_visual (one command)
cmake --build build -j$(nproc) # Desktop: all cores
```

### Tests (after full build passes)
```bash
ctest --test-dir build
QT_QPA_PLATFORM=offscreen ./build/test_visual
```

**Rule:** If build or tests fail → revert immediately. Do NOT push broken code.

### Why this matters
On PineTab 2 (RK3588S, 8 GB RAM):
- Full build + link with Qt6::Widgets: 30-60 sec (linker is bottleneck)
- Single file .o: 2-5 sec (ccache hit: <1 sec)
- `-fsyntax-only`: 1-3 sec (no codegen, no linker)

---

## Debugging — See What's Happening

### Instant signal tracing (zero code changes)
```bash
QT_LOGGING_RULES="qt.core.signal=true" ./build/progressive-desktop 2>&1
```
Shows every Qt signal emission. AI reads this to trace handler chains.
No code changes needed. Qt does this natively.

### F12 debug dump
Press F12 in the running app to print current state to stderr:
roomModel rows, timelineModel rows, currentRoomId, widget visibility.

### Channel logs
```cpp
#include "core/debug_log.hpp"

LOG(LogChannel::GUI,  "setCurrentRoom roomId=%s", roomId_.c_str());
LOG(LogChannel::SYNC, "/sync: %zu rooms", rooms.size());
LOG(LogChannel::E2EE, "decrypt failed for %s: no megolm session", eventId.c_str());
LOG(LogChannel::MEM,  "after-switch: %d events in timeline", model->rowCount());
LOG(LogChannel::NET,  "HTTP %d %s", statusCode, url.c_str());
```
Channels: GUI, SYNC, E2EE, NET, MEM, DBG.
Disable at compile time: `cmake -DPROGRESSIVE_DISABLE_LOG=ON ...`

### Assert with context
```cpp
PROGRESSIVE_ASSERT(!roomId_.empty(), "roomId empty when quickReact triggered");
```
Fails with: `[ASSERT] file.cpp:42: FAILED !roomId_.empty() — roomId empty when quickReact triggered`
Then aborts. Disable: `cmake -DPROGRESSIVE_DISABLE_ASSERT=ON ...`

### Function enter/exit tracer
```cpp
void MyHandler::doThing() {
    TraceFn _t("MyHandler::doThing");
    ...
}
// [TRACE] -> MyHandler::doThing
// [TRACE] <- MyHandler::doThing
```

### Tests without a server
`tests/fake_client.hpp` — FakeClient returns success responses.
Use in tests to verify handler logic without network or Matrix account.

---

## Don't Touch — Protected Files & Directories

### src/core/ — Qt-free zone (ALL files, .hpp included)
```
src/core/matrix_client.*
src/core/sync_engine.*
src/core/fast_sync.*
src/core/session_store.*
src/core/memory_stats.*
src/core/thread_pool.*
src/core/http_client.*
src/core/utils.hpp
src/core/crypto/*
```
**Rule:** No `#include <Q...>`, no `Q_OBJECT`, no Qt types in core/. Only `std::string`, `std::chrono`, `std::filesystem`, STL.

### src/core/thread_pool.hpp — immutable
- Singleton: `ThreadPool::instance().enqueue(task)` — already wired in 55+ places
- `#include "core/thread_pool.hpp"` is allowed in ANY file
- Never add/remove methods or change the singleton pattern

### Build system
```
CMakeLists.txt       — DO NOT MODIFY (unless creating new .cpp files — see below)
CMakePresets.json    — DO NOT MODIFY
cmake/               — DO NOT MODIFY
```

**EXCEPTION:** If you create a NEW .cpp file, you MUST add it to `add_executable(progressive-desktop ...)` in CMakeLists.txt. This is the ONLY allowed CMakeLists.txt edit. Add it in alphabetical order within the existing list.

---

## Include Path Convention

The build system has two include roots:

| Include root | How to include from anywhere |
|---|---|
| `src/` (progressive_core PUBLIC) | `#include "core/matrix_client.hpp"` |
| Source file's own directory | `#include "room/room_store.hpp"` (from `src/ui/` level) |

### From files at `src/ui/` level (main_window.cpp, auth_handler.cpp, etc.):
```cpp
#include "core/matrix_client.hpp"            // via src/ root
#include "room/room_store.hpp"              // relative to src/ui/
#include "timeline/timeline_model.hpp"       // relative to src/ui/
#include "chat/chat_view.hpp"               // relative to src/ui/
```

### From files in subdirectories (src/ui/chat/chat_view.cpp):
```cpp
#include "core/matrix_client.hpp"            // via src/ root
#include "../timeline/timeline_model.hpp"    // up one level, then into timeline/
#include "../room/room_store.hpp"            // up one level, then into room/
#include "message_edit.hpp"                  // same directory
```

### From files in src/ui/handlers/ (new directory):
```cpp
#include "core/matrix_client.hpp"            // via src/ root
#include "../room/room_store.hpp"            // up one level, then into room/
#include "../chat/chat_view.hpp"             // up one level, then into chat/
```

**Rule:** When creating files in a new subdirectory, always use `../` to reach sibling directories. Use the `src/` root for `core/`.

**NEVER write `"ui/xxx.hpp"` or `"ui/handlers/xxx.hpp"`.** This pattern depends on `src/` being the include root, works inconsistently, and breaks when files move. Always use relative paths:
- From `src/ui/handlers/` → `"../main_window.hpp"` (NOT `"ui/main_window.hpp"`)
- From `src/ui/handlers/` → `"../chat/chat_view.hpp"` (NOT `"ui/chat/chat_view.hpp"`)
- From `src/ui/handlers/` → `"auth_handler.hpp"` (same directory, no prefix)

---

## Namespace — MANDATORY

**EVERY new header and source file MUST be wrapped in:**
```cpp
namespace progressive::desktop {

// ... all code ...

} // namespace progressive::desktop
```
Without this, linking fails. Check existing files for the exact pattern.

---

## File Ownership — Who Owns What

| Handler | Files | Owns | Must NOT touch |
|---|---|---|---|
| **AuthHandler** | `src/ui/handlers/auth_handler.*` | Login, logout, forceReLogin, session management | Room list, timeline, chat |
| **ToolbarHandler** | `src/ui/handlers/toolbar_handler.*` | 9 toolbar actions, fullscreen toggle, chat logging | Room switching, sync |
| **RoomHandler** | `src/ui/handlers/room_handler.*` | Room click, switching, loadMore, invite accept/reject, context menus | Toolbar, sync, auth |
| **ChatView** | `src/ui/chat/chat_view.*` | Message display, input, send, quickReact, file attach | MainWindow, room model |
| **RoomStore** | `src/ui/room/room_store.*` | Room ops (loadHistory, rebuildFromSync, members) | **Widgets** — no show/hide/setText |
| **UILayoutBuilder** | `src/ui/ui_layout_builder.*` | Widget creation, layout construction | Business logic, sync |
| **E2eeInitHandler** | `src/ui/handlers/e2ee_init_handler.*` | Olm/Megolm init, device ID generation, crypto persistence | UI widgets |
| **SyncEngine** | `src/core/sync_engine.*` | /sync loop, token refresh, state tracking, crypto uploads | UI code |
| **FastSync** | `src/core/fast_sync.*` | simdjson /sync response parsing (zero-copy) | Network I/O, UI |

---

## Contract Boundaries — Verified Signals

### Signal chain (who emits, who receives)

```
MessageEdit → ChatView (Qt signals):
  sendMessage(std::string body)
  slashCommand(std::string command, std::string args)
  attachFileRequested()
  quickReact(QString emoji)

ChatView → MainWindow (Qt signal):
  slashCommandForward(std::string cmd, std::string args)

TimelineDelegate → MainWindow (Qt signals):
  imageClicked(QString eventId, QString mxcUrl)
  messageClicked(QString eventId)

MessageEdit → MainWindow (Qt signal):
  emojiPickerRequested()

AuthHandler → MainWindow (Qt signals):
  loggedIn()
  loggedOut()

RoomHandler → MainWindow (Qt signals):
  roomSwitchRequested(QString roomId)
  threadOpenRequested(QString rootEventId)
  threadCloseRequested()

RoomListDelegate → RoomHandler (Qt signals):
  inviteAccepted(QString roomId)
  inviteRejected(QString roomId)

SyncEngine → MainWindow (std::function callbacks, NOT Qt signals):
  SyncCallback = std::function<void(FastSyncResponse)>
  StateCallback = std::function<void(SyncEngineState, const SyncEngineStats&)>
  AuthErrorCallback = std::function<void()>
```

### Q_DECLARE_METATYPE requirement

Signals using `std::string` require this line in the **emitting** header:
```cpp
Q_DECLARE_METATYPE(std::string)
```
It's already in `message_edit.hpp:14`. If you add new signals with `std::string` parameters, add it to that new header too. Without it, the signal silently fails — no compile error, no runtime warning, just doesn't fire.

---

## Connect Syntax — ONE CORRECT WAY

**ALWAYS use new-style (compile-time checked):**
```cpp
connect(sender, &SenderClass::signalName, receiver, &ReceiverClass::slotName);
connect(sender, &SenderClass::signalName, this, [this](args) { ... });
```

**NEVER use old-style macros:**
```cpp
// WRONG — not compile-time checked, silently fails with std::string params
connect(sender, SIGNAL(foo()), receiver, SLOT(bar()));
```

**Exception:** `QComboBox::currentIndexChanged` has an overload. Use:
```cpp
connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), ...);
```

---

## MainWindow — Pure Orchestrator

MainWindow creates handlers and wires cross-handler signals. That's it.
Any logic >5 lines goes into a handler.

### Public getters (used by other files — NEVER remove):
```
threadBanner()      → QLabel*
threadBtn()         → QPushButton*
chatView()          → ChatView*
roomStore()         → RoomStore*
roomModel()         → RoomListModel*
timelineModel()     → TimelineModel*
statusLabel()       → QLabel*
inviteHead()        → QLabel*
messageEdit()       → MessageEdit*
timelineView()      → QListView*
placeholder()       → QLabel*
loadMoreBtn()       → QPushButton*
chatLogBtn()        → QPushButton*
roomHandler()       → RoomHandler*
```

### Files that call these getters:
```
RoomHandler:     roomStore(), chatView(), timelineView(), placeholder(), loadMoreBtn(), messageEdit(), threadBanner()
ToolbarHandler:  roomModel(), timelineModel(), chatLogBtn(), threadBtn()
AuthHandler:     statusLabel() (userLabel and other deps are constructor-injected, not via getters)
```

---

## Architectural Rules

### ThreadPool — usage pattern
```cpp
ThreadPool::instance().enqueue([guard = QPointer<QObject>(this), ...] {
    // NEVER call QWidget/QObject methods here — worker thread!
    auto result = heavy_computation();
    QMetaObject::invokeMethod(guard, [result] {
        // SAFE: back on UI thread
        updateUI(result);
    }, Qt::QueuedConnection);
});
```

### RoomStore — NEVER touch widgets
RoomStore methods return data or flags. They never call `show()/hide()/setText()/setVisible()`.

### simdjson API — CRITICAL
```cpp
// WRONG — will not compile or gives wrong results:
auto val = obj["room_id"];
std::string roomId = obj["room_id"];

// CORRECT — simdjson uses key_value_pair iteration:
for (auto [key, value] : obj) {
    if (key == "room_id") {
        std::string_view roomId = value.get_string().value();
    }
}
```
Never assign simdjson element to a variable directly. Always iterate.

### New file creation checklist
When creating `src/ui/handlers/foo_handler.hpp` and `.cpp`:
1. ✅ Wrapped in `namespace progressive::desktop { }`
2. ✅ `#pragma once` in header
3. ✅ `Q_OBJECT` in class with `public slots:` / `signals:` as needed
4. ✅ Constructor takes dependencies as pointers (no raw `new` of unrelated objects)
5. ✅ `.cpp` added to `add_executable(progressive-desktop ...)` in CMakeLists.txt
6. ✅ Header forward-declares classes, doesn't include unless needed
7. ✅ `ninja -C build CMakeFiles/progressive-desktop.dir/src/ui/handlers/foo_handler.cpp.o` passes

### Handler class template
```cpp
// src/ui/handlers/foo_handler.hpp
#pragma once
#include <QObject>
#include <string>

namespace progressive::desktop {

class MatrixClient;
class SomeDependency;

class FooHandler : public QObject {
    Q_OBJECT
public:
    FooHandler(MatrixClient* client, SomeDependency* dep, QObject* parent = nullptr);
public slots:
    void doThing(const std::string& param);
signals:
    void thingDone();
private:
    MatrixClient* client_;
    SomeDependency* dep_;
};

} // namespace progressive::desktop
```

---

## Coding Style

- **No comments** in implementation unless the code is genuinely confusing
- **Header comments are OK**: `// src/ui/handlers/foo_handler.hpp — one-line summary`
- `snake_case` for methods and member variables
- `PascalCase` for classes
- `#pragma once` for headers
- `namespace progressive::desktop { ... }` for ALL code
- C++20: `std::chrono`, `std::filesystem`, structured bindings
- Qt containers (`QString`, `QList`) ONLY in Qt-dependent files (`src/ui/`)
- Core (`src/core/`) uses STL exclusively

---

## Known Bugs — Concrete Info

### B1: quickReact button does nothing
- **Root cause:** `ChatView::roomId_` is empty when `doQuickReact` runs, causing silent return at `if (roomId_.empty()) return;`
- **What's confirmed working:** `RoomHandler::onRoomClicked:207` DOES call `chatView()->setCurrentRoom(...)`. The `onRoomClicked` method is correct.
- **What to check:** `connect(roomList_, &QListView::clicked, roomHandler_, &RoomHandler::onRoomClicked)` in `main_window.cpp:118` — verify the signal actually fires. Also check that `timelineModel_->clear()` at line 208 is not clearing `roomId_` (it doesn't — roomId_ is in ChatView, not model).
- **Debug:** add `std::fprintf(stderr, "[debug] quickReact roomId=%s\n", roomId_.c_str());` in `ChatView::doQuickReact` to see what roomId_ actually is at click time.

### B2: std::string signals not firing
- **Fix:** Add `Q_DECLARE_METATYPE(std::string)` at top of header (after includes, before class)
- **Already applied in:** `src/ui/chat/message_edit.hpp:14`
- **Needed in:** any NEW header that uses `std::string` in Qt signals

### B3: New encrypted rooms show encrypted=0
- **Root cause:** Incremental sync returns rooms with `state=0` (no state events in this sync batch)
- **Requires:** `RoomStore::batchLoadRoomStates()` call for rooms where `isEncrypted` is unknown
- **Location:** somewhere in `onSync` processing in `src/ui/main_window.cpp`

### B4: "no megolm session" when decrypting
- **Root cause:** `m.room_key` event arrives AFTER the encrypted message it encrypts
- **Needed:** a pending-event queue that retries decryption when a new `m.room_key` arrives
- **Does not exist yet** — this is a feature gap, not a regression you can break

---

## Test Suite

| Test | Executable | Needs Offscreen? | File |
|---|---|---|---|
| phase1 | test_phase1 | No | tests/test_phase1.cpp |
| phase4 | test_phase4 | No | tests/test_phase4.cpp |
| gui_phase4 | test_gui_phase4 | Yes (already set) | tests/test_gui_phase4.cpp |
| visual | test_visual | Yes (already set) | tests/test_visual.cpp |

**Note 1:** `test_gui_phase4.cpp` lines 31-115 contain a DUPLICATE of `RoomListModel`. If you modify the production model, the test copy won't reflect your changes.

---

## Known Issues — Not Bugs But Important

### Empty message bubbles for non-message events
`room_store.cpp:443-484` — `appendTimelineForRoom` creates `DisplayedEvent` objects for ALL event types, but the delegate only knows how to render `m.room.message`. Unknown types (m.room.member for others, m.room.topic, m.room.name, m.room.encryption, m.typing, m.receipt) produce empty bubbles. **Adding a new event type without a render path in the delegate or a filter in the model will cause empty bubbles.**

### chatLogging_ duplication
Both `RoomHandler` and `ToolbarHandler` have `chatLogging_` / `chatLogFile_` members. Figure out who really owns chat logging before modifying either file.

### reactionClicked signal not connected
`TimelineDelegate::reactionClicked` exists in `timeline_delegate.hpp:34` but has ZERO `connect()` calls anywhere. Clicking a reaction pill in the timeline does nothing.

---

## simdjson Usage — Core Files Only

simdjson is used in `src/core/fast_sync.*`. Its DOM API is verbose but fast.
The header `fast_sync.hpp` forward-declares simdjson types to avoid pulling the
full library into every translation unit.

```cpp
// In header (fast_sync.hpp):
namespace simdjson::dom { class parser; class element; }

// In implementation (fast_sync.cpp):
#include <simdjson.h>
```

When adding simdjson parsing code, always put the `#include <simdjson.h>` in the `.cpp` file, not the `.hpp` header. This keeps compile times fast.

---

## Git — Before You Commit

```bash
# 1. Check what you changed:
git status

# 2. Check for untracked files you forgot to add:
git status --short | grep "^??"

# 3. Stage everything:
git add <files>

# 4. Verify before committing:
git diff --cached --stat

# 5. Commit:
git commit -m "short description of what changed"
```

Never `git commit --amend` after `git push` unless you also `git push --force`.
Never `git push --force` on a shared branch.
This repo is single-developer — both are safe. Still check `git status` before amending.
