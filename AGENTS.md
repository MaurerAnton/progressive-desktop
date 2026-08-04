# AGENTS.md — Critical Coding Rules for Progressive Chat

**Read this file, code_map.json, and memory/REFERENCE.md before any code change.**
**memory/DREAM.md explains WHY the architecture exists.**
**Last updated: August 3, 2026**

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

## Dependencies
- libsodium (system package) — ed25519 keypair generation + curve25519/AEAD for
  cross-signing (Phase 6) and key backup (Phase 7). Arch/PineTab: `pacman -S libsodium`.
  CI installs `libsodium-dev`. Linked to progressive_core via PkgConfig::SODIUM.
- **libsodium AArch64 quirks (PineTab-class boxes, found Aug 4)**: (1)
  `crypto_sign_ed25519_seed_keypair` SEGFAULTS on some AArch64 builds — for the
  key backup, the recovery seed IS the curve25519 secret (m.megolm_backup.v1),
  no ed25519 conversion needed; (2) `crypto_box_seal_open` SEGFAULTS with a NULL
  pk AND uses the pk's VALUE — always derive the recipient pk from the sk
  (`crypto_scalarmult_curve25519_base`) and pass it.

## Build & Test

```bash
# After each change (fast):
ninja -C build CMakeFiles/progressive-desktop.dir/src/ui/handlers/room_handler.cpp.o

# Full build + tests (before push):
./scripts/build.sh all

# User's command on PineTab:
git pull && git submodule update --init --recursive && cmake --preset pinetab2 && cmake --build build -j4 && ./build/progressive-desktop
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

### src/core/ portability (Qt-free)

`src/core/` MUST remain Qt-free. Rationale: Android NDK and WebAssembly
builds link `progressive_core` without Qt — any Qt include in core fails
the build (link-time enforcement: `progressive_core` does not link Qt).
CI guard: `scripts/check_no_qt_in_core.sh` (grep-based, runs in <1s).
This protects the sister-project design (`progressive-android-next`)
and a future WASM target (JS UI + Embind to progressive_core).

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

### JSON request bodies: NEVER repeat a user key (group per-user maps)
Matrix request bodies are JSON objects — a duplicate key is silently resolved by the
server (it keeps the LAST one), silently dropping the first. Building per-device bodies
with a loop like `"@user":{dev1},"@user":{dev2}` LOSES one device. **Bit us TWICE in one
session (Aug 3):** the `/keys/claim` body and the `/sendToDevice` body in
`shareRoomKey` each wrote one entry per device, so multi-device members randomly never
got room keys — the multi-device CI test caught both. Rule:
- `/keys/query`, `/keys/claim`, and `/sendToDevice` bodies with per-device entries MUST
  group by user: `{"@user":{"dev1":...,"dev2":...}}`.
- `shareRoomKey` (decryptor.cpp) is the reference implementation.
- `/keys/signatures/upload` bodies have TWO extra traps: the key under a user
  must be the target's BARE master pub (`master_key_id.split(":",1)[1]`), and
  the content's `signatures` map must be keyed by the SIGNER's user id (bob's
  master signed by alice -> `signatures["@alice"]`) — `buildCrossSigningContent`
  takes a `signerUserId` param for this.
- The multi-device CI scenario (`test_multiaccount_multidevice`) is the guard; new
  multi-device JSON endpoints should extend it or be added to it.

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

    - `olm_unpickle_session` / `olm_unpickle_account` / `olm_unpickle_pk_decryption` — take `void *pickled`
      (NOT const). libolm decrypts the pickle buffer IN-PLACE — after the call, the original buffer contains
      garbage (decrypted plaintext, not the encrypted pickle). MUST pass a copy:
      `std::string copy = original; sess.unpickle("", copy);`. Passing the stored data directly destroys it;
      every subsequent unpickle attempt fails because the buffer is now garbage. Discovered July 26 — the
      7th libolm quirk, cost a full debugging cycle. Same applies to `olm_unpickle_account` and `olm_unpickle_pk_decryption` — always pass a copy.

    - `olm_sas_set_their_key` — decodes the other party's SAS pubkey BASE64 IN-PLACE (same `b64_input`
      pattern). After the call, the caller's pubkey buffer is GARBAGE. MUST pass a copy:
      `std::string copy = theirPubkey; sas.setTheirKey(copy);`. Passing the stored pubkey directly corrupts it —
      the caller's `theirSasPubkey` member becomes garbage and subsequent emoji/MAC computation fails. Discovered
      August 1 by the two-manager SAS protocol test (`a91ca63`). 9th libolm quirk.

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

### Rule: Never cache mutable credentials — read live from MatrixClient
Access tokens rotate during runtime (pre-refresh at startup, sync-triggered re-authentication at `sync_engine.cpp:156`). **NEVER store `accessToken`, `userId`, `deviceId`, or `homeserverUrl` in a class member variable for later HTTP use.** Either:
(a) receive them as function parameters (like `shareRoomKey`, `chat_view.cpp:195-198`), OR
(b) hold `shared_ptr<MatrixClient>` via `setClient()` (with full propagation chain, per rule #3), and read from `client->account()` at call time.

**The `ctxToken_` bug (#14):** `Decryptor` stored the token at E2EE init (`e2ee_init_handler.cpp:55`), but the pre-refresh (`session_bootstrap.cpp:76`) rotated it 30 lines later. `ctxToken_` held the stale value → all `forceNewOlmSession`/`requestRoomKey` HTTP calls got 401 → Bug A recovery chain stalled. Fixed via re-calling `setCryptoContext` after each refresh (`a33e44e`), but the real permanent fix (deferred) is injecting `shared_ptr<MatrixClient>` and reading `accessToken` live — matching Nheko's `http::client()` pattern.

### Rule: Classes making authenticated HTTP calls MUST have MatrixClient access
If a class calls `httpPost`/`httpPut` with auth headers (`makeAuthHeaders(token)`), it MUST either accept credentials as parameters or hold `shared_ptr<MatrixClient>`. **NEVER cache the token in a member** — that's the `ctxToken_` workaround which broke on rotation. The `shareRoomKey` pattern (pass `accessToken` as a parameter, read fresh from `client->account()` at call time) is correct.

---

## E2EE Implementation Status (August 1, 2026)

### Working ✓
- Inbound decryption (Olm + Megolm, recovery chain via m.dummy + m.room_key_request)
- Outbound encryption (Megolm + Olm, room_key sharing via /sendToDevice)
- Self-echo decryption (outbound imported as inbound)
- Olm session persistence (multiple sessions per senderKey, vector storage, scoped per-account, dedup, cap 20/sender)
- Megolm inbound session persistence (SQLite, scoped per-account)
- Outbound Megolm persistence (SQLite, scoped per-account, roomKeysShared flag)
- Token rotation handling (setCryptoContext re-call at refresh sites)
- Bug A recovery verified (Element re-shares room_key via fresh Olm session)
- Bug B outbound verified (Element decrypts; room_id in Megolm plaintext)
- **Multi-account E2EE** — `shared` flag on OlmAccount (matrix-rust-sdk pattern), per-account scoping for all crypto tables
- **OTK count tracking** — `uploadedKeyCount_` updated from /sync, smart generation (`max(0, 100 - serverCount)`)
- **device_lists tracking** — parse `device_lists:{changed,left}` from /sync, mark stale users
- **Device reset** — "Reset device keys" action clears stale OTKs via `POST /delete_devices`
- **Ed25519 signature verification** — `olm_ed25519_verify` (PUBLIC stable libolm API, olm.h:516). Replaces submodule stub.
- **Olm plaintext validation** — verify sender/recipient/recipient_keys per m.olm.v1 spec
- **OTK + device key signature verification** — verify signed_curve25519 OTK signatures + device_keys signatures on /keys/query + /keys/claim
- **SAS verification (m.sas.v1, Phase 2)** — full request→ready→start→accept→key→mac→done/cancel state machine, OlmSAS crypto, commitment + MAC verification over the other side's keys, to-device + in-room routing, two-manager protocol test (`test_e2ee_verify_protocol.cpp`) green
- **Fallback keys (Phase 3)** — generateFallbackKey/unpublishedFallbackKey/forgetOldFallbackKey (submodule API), /sync trigger, per-account cooldowns + gradual backoff (60s→30m→stop), live-Synapse claim test
- **Key sharing + forwarded keys (Phase 4)** — m.room_key_request handling (`handleRoomKeyRequest`, verified-only policy via `verified_devices` table), m.forwarded_room_key (`handleForwardedRoomKey`), export/import (`exportAllKeys`/`importKeys`, MegolmSessionData envelope), Olm-encrypted key exchange (`sendOlmToDevice` with signature-verified device keys), pending-event replay
- **Megolm rotation (Phase 5)** — `isRotationDue` + m.room.encryption config (messageCount/startTimeMs), re-share on rotate
- **Cross-signing (Phase 6 core)** — libsodium ed25519 keygen/sign/verify, `POST /keys/device_signing/upload` with UIA (password) retry (`setupCrossSigningWithPassword`), device_keys re-upload with SSK signature (full canonical message, M_INVALID_SIGNATURE-verified), published-state guard via /keys/query
- **Phase 6 tail DONE (Aug 3)** — trust computation (`computeDeviceTrust`: device_keys SSK-signature verification against the published self_signing_keys from /keys/query), SAS MSK exchange (the master key as a pseudo-device in the SAS mac, symmetric inclusion, sorted KEY_IDS), cross-user cross-signing after a verified SAS (sign the other user's master key with our USK → `POST /keys/signatures/upload`, body key = the target's BARE master pub), UI device shields (green = SAS-verified, grey = SSK cross-signed, red = unverified) in PrefsDialog devices + RoomMembersDialog, cross-signing reset flow (`SyncEngine::resetCrossSigning` + PrefsDialog button, UIA-aware). Live-Synapse CI covers the whole chain: A1↔A3 self-verification, cross-user MSK SAS, the USK cross-signature, and the verified-only key-share policy (verified honored, unverified denied)
- **Live-Synapse E2EE integration test (CI)** — `test_synapse_e2ee.cpp` registers 3-4 real users on a Synapse container, creates an encrypted room, shares the room key, and decrypts cross-account: 2-user round-trip, fallback claim (dedicated fresh user — Synapse ADDS OTKs, never replaces), rotation + key-request loop, cross-signing setup, and the multi-account/multi-device scenario (3 members, alice on 2 devices via /login, sender's own other devices decrypt, late joiner). Guarded by `.github/workflows/synapse-e2ee.yml`. Skips (exit 0) when `SYNAPSE_URL` unreachable so local `ctest` stays 100%.

### E2EE test-infra caveats (known, accepted)
- **The UIA 401-retry IS exercised in CI** — the mm test's `publishCrossSigning` re-uploads
  FRESH keys to an account that already has cross-signing, which Synapse requires UIA for
  (`SigningKeyUploadServlet`, `can_skip_ui_auth=False`) → the first attempt 401s and the
  test's `m.login.password` retry runs on every CI run. What has NO automated coverage is
  the APP's own `SyncEngine::setupCrossSigningWithPassword` flow (the test has its own
  copy of the retry logic). The first-attempt httpStatus is logged by the test
  (`[synapse-test] publishCrossSigning first attempt http=`).
- **The test's UIA logic is a DUPLICATE** of `SyncEngine::setupCrossSigningWithPassword`
  (the test drives client+decryptor directly, no SyncEngine) — the two can diverge.
- **The synapse test proves the CORE, not the app glue** — it drives the decryptor
  directly, bypassing SyncEngine/UI wiring. App-level flows (send path → shareRoomKey,
  invite UI) are manually tested only.

### CI infrastructure notes
- Both workflows use ccache (launcher in the ci preset, persisted via actions/cache) and
  cache `build/_deps` + the sqlite amalgamation (keyed on `cmake/**`); the Synapse
  workflow is gated on E2EE-relevant paths. If a `cmake/**` change or a CONFIGURATION
  failure ever appears right after a cancelled run, suspect a partial `build/_deps`
  restore (the sqlite self-heals, git clones don't) — delete the `deps-*` cache entry in
  the Actions UI and re-run.

### In Progress 🔄
- **SAS UI polish** — dialog + handler exist (SasVerificationDialog, VerificationHandler, RoomMembersDialog → Verify, PrefsDialog → Your devices). Cross-client verification against Element/FluffyChat is next.

### Gaps
| Gap | What | Priority |
|---|---|---|
| Device verification (cross-signing) | Setup + publishing done (Phase 6 core); trust chain (SAS MSK exchange, device shields, cross-signing reset flow) deferred | Phase 6 tail |
| SSSS key backup | Can't recover history after re-login; also unlocks cross-device secret sharing (device2 gets SSK → the mm test's `!a2SskSig` assertion must be REMOVED then) | Phase 7 |
| Invite/add-member into EXISTING room | `MatrixClient::inviteUser` exists (test-only) but no UI calls it; planned as part of a command system (/invite, /ban, /confetti — user's Element-style copy-paste plan) | Later |
| Threading: HTTP on UI thread | requestRoomKey blocks UI for 1-3s | LOW |
| Option A refactor | ctxToken_ works but is stale-prone; inject MatrixClient | LOW |
| FluffyChat multi-device OTK | Only claim 1 OTK for 2+ devices | LOW |
| Proactive Olm session creation | get_missing_sessions() between syncs | P5 (deferred) |
| Ed25519 signature verification | Submodule stub returned true — replaced with real libolm API | ✅ DONE (Phase 1) |
| Olm plaintext validation | Verify sender/recipient/keys per spec | ✅ DONE (Phase 1) |
| OTK signature verification | Verify signed_curve25519 signatures | ✅ DONE (Phase 1) |
| SAS device verification | m.sas.v1 state machine + crypto + dialog + protocol test | ✅ DONE (Phase 2) |
| Outbound Megolm persistence | Sessions lost on restart | ✅ DONE (Phase 1 bonus) |
| Olm session exponential growth | 30000+ sessions from switchAccount append-without-clear | ✅ DONE (fixed: clear before load + dedup + cap 20/sender) |
| SIGSEGV on close | Detached sync thread use-after-free | ✅ DONE (join() instead of detach()) |
| Ctrl+Tab → Logout | Account cycling landed on "Logout" combo item | ✅ DONE (cycle accounts only) |
| clearAccount() nukes all accounts | DELETE FROM account with no WHERE | ✅ DONE (WHERE user_id = ?) |

### Multi-Account E2EE Patterns (AGENTS.md #7)
1. **`shared` flag**: Stored on OlmAccountStore (matches matrix-rust-sdk `Account.shared`). `false` = new account → upload device_keys. `true` = already uploaded → skip. Persisted in `olm_account.shared` column.
2. **Per-account scoping**: All crypto tables use `userId/deviceId` as key suffix: `megolm:userId/devId`, `olm_sessions:userId/devId`, `otk_uploaded_once:userId/devId`. No cross-contamination when switching accounts.
3. **OTK count from /sync**: `fast_sync.cpp` parses `signed_curve25519 count` → `uploadedKeyCount_` → `needed = max(0, 100 - serverCount)`. Never generate duplicate OTKs.
4. **Double-init guard**: NEVER call `init()` before `init(pickle, key, shared)`. The 2-arg init handles both load and create-new cases. Pre-calling `init()` creates an account that `load()` can't overwrite → libolm zeros the struct.
5. **`sessionCount()` inside `unpickleAll()`**: Already holds `mtx_` → don't call `sessionCount()` (which locks). Use `impl_->mgr.sessionCount()` directly.
6. **`markOneTimeKeysPublished()` before generate**: Always discard old unpublished OTKs before generating new ones. Prevents sequential ID collisions (400 "already exists").
7. **`device_lists` from /sync**: Parse `device_lists:{changed,left}` in `fast_sync.cpp`. Mark users as stale in `Decryptor::staleDeviceUsers_`. LOG on next `shareRoomKey` call.
See `docs/E2EE.md` for full multi-account E2EE architecture and the 167 stale OTKs cautionary tale.

### E2EE Submodule Audit — FAKE Boilerplate Trap Warning
The `third_party/progressive-android-experiments/` submodule has ~90 E2EE-relevant files. ~12 are REAL (libolm-backed, safe to port). ~30+ are FAKE (auto-generated JSON-echo boilerplate). **Do NOT port files named `*_v4.cpp`, `crypto_ops.cpp`, `sas_manager.cpp`, `backup_controller.cpp`, `gossip_manager.cpp`, `key_export_utils.cpp`, `dehydrate_utils.cpp`, or `secret_*` without verifying they contain real crypto logic.** These have plausible names and large file sizes but are no-ops. A filename-based audit badly overstates what's there. See `docs/E2EE.md` for the full REAL/FAKE inventory.

### REAL submodule files safe to port:
- `sas_verification.cpp` (212L) — OlmSAS wrapper (13 `olm_sas_*` calls) → ✅ PORTED (`sas.cpp`/`sas.hpp`)
- `verification_utils.cpp` (156L) — 64-emoji table + computeSasEmojis + builders → ✅ PORTED (`sas_emojis.cpp`, `verification.cpp`)
- `keyshare.cpp` (103L) — incoming m.room_key_request handling + m.forwarded_room_key builders
- `room_encryption.cpp` (123L) — Megolm rotation policy (`isEncryptionRotationDue`)
- `key_backup.cpp` (329L) — recovery key format (base58, parity, curve-key) — NOT backup crypto
- `cross_signing_manager.cpp` (342L) — trust-chain DATA MODEL only (no crypto — keygen/sign/verify are STUBS)
- `crypto_algorithms.cpp` — SHA/HMAC/HKDF primitives
- `olm_session.cpp:157` — fallback key (on OlmAccountData API, needs port to OlmAccount class)

### libolm Quirks Documented (AGENTS.md #6) — full details in docs/E2EE.md
1. olm_create_inbound_session expects BASE64
2. olm_decrypt expects BASE64
3. olm_encrypt outputs BASE64
4. olm_init_inbound_group_session / olm_import_inbound_group_session expect BASE64
5. olm_group_encrypt outputs BASE64
6. olm_group_decrypt_max_plaintext_length clobbers buffer IN-PLACE
7. olm_*_session(memory) zeros the struct — call ONCE
8. olm_unpickle_session mutates pickle buffer IN-PLACE — must pass copy
9. olm_sas_set_their_key decodes pubkey base64 IN-PLACE — must pass copy
10. olm_account_unpublished_fallback_key returns the empty form {"curve25519":{}} (length > 0) even when no fallback key exists — detect by content, not length (account.cpp:359-366)
See `docs/E2EE.md` for full implementation notes, common mistakes, recovery chain design, and spec compliance table.

---

## FINAL CHECK — Before Responding "Done"

```bash
git push 2>&1 | grep -q "Everything up-to-date\|-> main" || { echo "NOT PUSHED"; false; }
ctest --test-dir build 2>&1 | grep -q "100% tests passed" || { echo "TESTS BROKEN"; false; }
echo "[OK] Pushed. git pull && git submodule update --init --recursive && cmake --preset pinetab2 && cmake --build build -j4 && ./build/progressive-desktop"
```
