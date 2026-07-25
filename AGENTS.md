# AGENTS.md — Critical Coding Rules for Progressive Chat

**Read this file, code_map.json, and memory/REFERENCE.md before any code change.**
**memory/DREAM.md explains WHY the architecture exists.**
**Last updated: July 24, 2026**

---

## CRITICAL — Violating These Causes Crashes

### 1. Always push after commit
```bash
git add <files> && git commit -m "..." && git push
```
If you forget push → fix never reaches user. **#1 cause of "the fix didn't work".**

### 2. shared_ptr for MatrixClient + SessionStore (NEVER raw pointer)
```cpp
std::shared_ptr<MatrixClient> client_;   // right
MatrixClient* client_;                   // WRONG — crashes in ThreadPool lambdas
```
All handlers/stores use shared_ptr. Dialogs may keep raw (short-lived).

### 3. setClient/setSessionStore MUST propagate — FULL CHAIN
**Every class that stores `client_` MUST have `setClient()`.**
**Every parent that creates a child with `client_` MUST propagate `setClient()` to it.**

```cpp
// In child class header:
void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

// In parent class header — propagate to ALL children:
void setClient(std::shared_ptr<MatrixClient> c) {
    client_ = std::move(c);
    if (child1_) child1_->setClient(client_);  // REQUIRED
    if (child2_) child2_->setClient(client_);  // REQUIRED
    // ... for EVERY child that stores client_
}

// In MainWindow::setClient() — register every handler/child:
void MainWindow::setClient(std::shared_ptr<MatrixClient> client) {
    client_ = std::move(client);
    if (handler1_)  handler1_->setClient(client_);
    if (handler2_)  handler2_->setClient(client_);
    // ... for EVERY handler created in MainWindow
}
```

**B21, B38, B39 (RoomContextMenu), B40 (ThreadHandler) all happened because this chain broke.**
The pattern: class A creates class B with client_ → A::setClient updates A's client_ but NOT B's → B has stale client_ → crash.

### 4. setClient AUDIT — run before every push
```bash
# Find classes with client_ but NO setClient:
grep -rn "shared_ptr<MatrixClient> client_" src/ --include="*.hpp" | cut -d: -f1 | sort | while read f; do
    grep -q "void setClient" "$f" || echo "MISSING setClient in $f"
done
# Find parent classes that create children but don't propagate:
grep -rn "= new.*client_" src/ --include="*.cpp"
# Verify every= line matches a setClient propagation in the parent
```
**B21, B38, B39 (RoomContextMenu), B40 (ThreadHandler) — all fixed by adding setClient() + propagation.**

### 5. cd to project root first
All paths relative to project root: `src/ui/room_store.cpp` not `/home/user/progressive-desktop/...`

### 6. Read before edit. Build after each change.
```bash
ninja -C build CMakeFiles/progressive-desktop.dir/src/ui/<path>/<file>.cpp.o
```
If build or tests fail → revert. Never push broken code.

---

## Build & Test

```bash
# After each change (fast):
ninja -C build CMakeFiles/progressive-desktop.dir/src/ui/handlers/room_handler.cpp.o

# Full build + tests (before push):
./scripts/build.sh all

# User's command on PineTab:
git pull && cmake --preset pinetab2 && cmake --build build -j4 && ./build/progressive-desktop
```

---

## File Structure — Where Everything Lives

| Directory | Purpose | New files go here? |
|---|---|---|
| `src/core/` | Qt-free Matrix logic | No Qt includes allowed |
| `src/ui/handlers/` | Business logic | ✅ New handlers |
| `src/ui/chat/` | Chat widgets | ✅ Chat UI |
| `src/ui/timeline/` | Message rendering | ✅ Layout/painter |
| `src/ui/room/` | Room data + models | ✅ Data stores |
| `src/ui/dialogs/` | Modal dialogs | ✅ New dialogs |
| `src/ui/shared/` | Theme, images, notifications | ✅ Shared utilities |
| `tests/` | Test files | ✅ New tests |

## Code Rules

### Every .hpp file
- `#pragma once`
- `namespace progressive::desktop { ... }`
- `Q_OBJECT` if QObject-derived
- Forward-declare dependencies, max 5 `#include`s
- Max 350 lines (UI), 300 (core) — split if larger

### Every .cpp file
- Full `#include`s go here (not in header)
- Constructor takes `std::shared_ptr` for MatrixClient/SessionStore
- Add new .cpp to CMakeLists.txt in alphabetical order

### Include paths (NEVER "ui/xxx.hpp")
```
From src/ui/ → "core/matrix_client.hpp" + "room/room_store.hpp"
From src/ui/handlers/ → "../room/room_store.hpp" + "auth_handler.hpp" (same dir)
From src/ui/chat/ → "../timeline/timeline_model.hpp" + "message_edit.hpp" (same dir)
```

### Connect syntax
```cpp
connect(sender, &Class::signal, receiver, &Class::slot);          // OK
connect(sender, &Class::signal, this, [this](args) { ... });      // OK
connect(sender, SIGNAL(foo()), receiver, SLOT(bar()));             // WRONG
```

### ThreadPool pattern
```cpp
ThreadPool::instance().enqueue([guard = QPointer<QObject>(this), client = client_] {
    auto result = client->someCall();
    QMetaObject::invokeMethod(guard, [result] { updateUI(result); }, Qt::QueuedConnection);
});
```

### simdjson: iterate, never direct-assign
```cpp
for (auto [key, value] : obj) { if (key == "...") ... }
```

---

## Top 5 Bug Patterns AI Creates

1. **setClient chain broken**: Parent creates child with client_ → parent::setClient doesn't propagate to child → child has stale client_ → crash. Triple-check: (a) child has setClient(), (b) parent::setClient calls child->setClient(), (c) MainWindow::setClient registers parent. **Happened 4 times: B21, B38, B39, B40.**
2. **LifeToken forgotten**: Async callback runs after caller destroyed. Fix: `shared_ptr<bool>` + `if (!*token) return;`.
3. **#include in header**: Changes header → recompiles everything. Fix: forward-declare, full include in .cpp.
4. **"ui/xxx.hpp" include path**: Works through src/ root but breaks when files move. Fix: use relative `../` paths.
5. **No Q_OBJECT**: New QObject-derived class without Q_OBJECT → signals/slots silently fail.

6. **libolm data format — verified from C source** (`build/_deps/olm-src/src/`):
   - `olm_create_inbound_session` → calls `b64_input()` (olm.cpp:567) — expects BASE64. NEVER decode before.
   - `olm_decrypt` → calls `b64_input()` (olm.cpp:779) — expects BASE64. NEVER decode before.
   - `olm_encrypt` → outputs BASE64 via `b64_output` (olm.cpp). No post-encode needed.
   - `olm_init_inbound_group_session` AND `olm_import_inbound_group_session` → BOTH call `_olm_decode_base64` (inbound_group_session.c:175,196) — expect BASE64. NEVER pre-decode. `init` is for v2 session_key (m.room_key), `import` is for v1 export format.
   - `olm_group_encrypt` → outputs BASE64 via `_olm_encode_base64` (outbound_group_session.c:310). No post-encode needed.
   - `olm_group_decrypt_max_plaintext_length` → clobbers message buffer IN-PLACE via `_olm_decode_base64`. MUST `memcpy`-restore before calling `olm_group_decrypt`.
   - `olm_*_group_session(void* memory)` AND `olm_account()` AND `olm_session()` → NOT pure casts. They call `olm_clear_*` / `olm::unset` which ZEROS the struct. Call ONCE on malloc'd memory, then use `static_cast<::...*>` for subsequent access. Re-calling destroys the session.

   Matrix JSON stores all Olm/Megolm fields as base64. Correct chain: `JSON body (base64) → libolm fn(base64) → internal b64_input/_olm_decode_base64 → raw → crypto`.
   **NEVER add `base64Decode()` before any olm_* function — they all handle base64 internally.**
   **NEVER re-call `olm_*_session(memory)` on already-initialized memory — use `static_cast`.**

---

## Debugging

```cpp
#include "core/debug_log.hpp"
LOG(LogChannel::GUI, "msg=%s", str);
PROGRESSIVE_ASSERT(cond, "reason");
TraceFn _t("functionName");  // -> on enter, <- on exit
```
Channels: GUI, SYNC, E2EE, NET, MEM, DBG. F12 = dump state.

---

## Preventing Regressions (no "it worked before, now it's broken")

### Rule: Mechanical validation beats memory
- Every new class with `client_` → add `setClient()` BEFORE writing implementation
- Every new child created in a parent → add `child->setClient(client_)` in `parent::setClient()` BEFORE creating the child
- Run the `setClient AUDIT` script above after creating any new handler/store

### Rule: Small commits, single change each
- One bug per commit, one feature per commit
- If a commit touches >3 files → split it
- Makes `git bisect` useful when something breaks
- **Never: `fix crash + add dialog` in same commit**

### Rule: Prompt MUST include audit step
Every AI coder prompt that touches `client_` or creates a new handler MUST include:
```bash
# Run before commit — zero output required:
grep -rn "shared_ptr<MatrixClient> client_" src/ --include="*.hpp" | cut -d: -f1 | sort -u | while read f; do
    grep -q "void setClient" "$f" || echo "MISSING setClient in $f"
done
```
Zero output = safe. Any "MISSING" = DO NOT COMMIT, add setClient() first.

### Rule: Test what you change
- Every new handler → 15-line smoke test in `tests/test_visual.cpp`
- Every fix → verify with the exact reproduction steps from the bug report
- Build + ctest before push (see FINAL CHECK below)

### Tech debt: allowed but tracked
- If a quick fix adds debt (duplication, hardcoded value, skipped edge case), add `// DEBT: <reason>` comment
- DEBT comments must reference a bug number or AGENTS.md rule
- Example: `// DEBT(B3): batchLoadRoomStates queries all rooms on every sync — add stateLoaded flag`
- CI build remains green — tech debt does NOT mean broken tests

---

## FINAL CHECK — Before Responding "Done"

```bash
git push 2>&1 | grep -q "Everything up-to-date\|-> main" || { echo "NOT PUSHED"; false; }
ctest --test-dir build 2>&1 | grep -q "100% tests passed" || { echo "TESTS BROKEN"; false; }
echo "[OK] Pushed. git pull && cmake --preset pinetab2 && cmake --build build -j4 && ./build/progressive-desktop"
```
