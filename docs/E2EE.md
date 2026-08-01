# docs/E2EE.md — End-to-End Encryption Implementation Notes

> **Who this is for:** Developers implementing Matrix E2EE with libolm in any language.
> Written from hard-won experience implementing E2EE from scratch in a pure C++20 Matrix client.
> **Last updated:** August 1, 2026 — Phase 1 complete (ed25519 verify, Olm plaintext validation, OTK/device key signature verified). Phase 2 complete (SAS m.sas.v1 state machine, crypto, dialog, two-manager protocol test green). Live-Synapse E2EE integration test green in CI.
> See `docs/E2EE-troubleshooting.md` for diagnostic guide (log patterns → root cause → fix).

---

## Architecture Overview

Progressive Chat's E2EE is a pure C++20 implementation using libolm. Qt-free core (`src/core/crypto/`).

### Component layers

```
┌─────────────────────────────────────────────────┐
│ UI layer (chat_view, timeline)                  │
│   sendEncryptedEvent → encryptMessage           │
│   decryptMegolmEvent ← decrypted plaintext      │
├─────────────────────────────────────────────────┤
│ Decryptor (decryptor.cpp — coordinator)         │
│   OlmAccount → device keys, one-time keys       │
│   OlmSession → 1:1 encryption (to-device)       │
│   Megolm inbound → group message decryption     │
│   Megolm outbound → group message encryption    │
│   room_key sharing → /sendToDevice              │
│   recovery chain → m.dummy + m.room_key_request │
├─────────────────────────────────────────────────┤
│ libolm C library (build/_deps/olm-src/src/)     │
│   olm.cpp, session.cpp, inbound_group_session.c │
│   outbound_group_session.c, base64.cpp          │
└─────────────────────────────────────────────────┘
```

### Key flows

1. **Sending:** Megolm outbound encrypts plaintext → `m.room.encrypted` in room timeline. Room key shared via Olm 1:1 to all member devices.
2. **Receiving:** Olm 1:1 decrypts to-device `m.room_key` → Megolm inbound session created → room timeline `m.room.encrypted` decrypted.
3. **Recovery:** If Olm session missing → send `m.room_key_request` + `m.dummy` (force Olm handshake) → sender re-shares room_key → decrypt.

---

## libolm Quirks — 9 Critical Data-Format Rules

All verified from the C source at `build/_deps/olm-src/src/`. Each has caused a multi-hour debugging session when violated.

### Quirk 1: `olm_create_inbound_session` expects BASE64

```c
// olm.cpp:567 — internals call b64_input():
size_t olm_create_inbound_session(
    OlmSession *session, OlmAccount *account,
    void const *one_time_key_message, size_t message_length)
{
    // b64_input decodes BASE64 internally
    size_t raw_length = b64_input(one_time_key_message, message_length, tmp);
    // ...
}
```

**Correct:** Pass the `m.room.encrypted` ciphertext from Matrix JSON as-is (already base64).
```cpp
// Matrix JSON body: {"sender_key":"...","ciphertext":"BASE64_STRING"}
std::string ciphertext = contentJson["ciphertext"].get_string();
olm_create_inbound_session(session, account, ciphertext.data(), ciphertext.size());
// ✅ libolm handles base64 → raw internally
```

**WRONG:** DON'T `base64Decode()` before passing.
```cpp
ciphertext = base64Decode(ciphertext);
olm_create_inbound_session(session, account, ciphertext.data(), ciphertext.size());
// ❌ DOUBLE-DECODE — libolm gets garbage, session creation fails
```

### Quirk 2: `olm_decrypt` expects BASE64

Same as Quirk 1. `olm_decrypt` → `b64_input()` (olm.cpp:779). Pass Matrix JSON ciphertext as-is.

### Quirk 3: `olm_encrypt` outputs BASE64

`olm_encrypt` calls `b64_output` internally. The output IS already base64-encoded. Insert directly into Matrix JSON ciphertext field — NO additional encode step.

### Quirk 4: Megolm init/import both expect BASE64

```c
// inbound_group_session.c:175 — init
size_t olm_init_inbound_group_session(..., uint8_t const *session_key, size_t session_key_length) {
    _olm_decode_base64(session_key, session_key_length, session->initial_ratchet);
}
// inbound_group_session.c:196 — import
size_t olm_import_inbound_group_session(..., uint8_t const *session_key, size_t session_key_length) {
    _olm_decode_base64(session_key, session_key_length, session->initial_ratchet);
}
```

- `olm_init_inbound_group_session` is for v2 session_key from `m.room_key` to-device events.
- `olm_import_inbound_group_session` is for v1 export format (key export/backup files).
- **BOTH call `_olm_decode_base64` internally** — pass BASE64 as-is.

### Quirk 5: `olm_group_encrypt` outputs BASE64

`outbound_group_session.c:310` → `_olm_encode_base64`. Output is base64. Insert directly into `m.room.encrypted.ciphertext`.

### Quirk 6: `olm_group_decrypt_max_plaintext_length` CLOBBERS message buffer IN-PLACE

```c
// inbound_group_session.c — max_plaintext_length calls _olm_decode_base64
// which overwrites the message buffer with decoded raw bytes.
size_t olm_group_decrypt_max_plaintext_length(
    size_t message_index, uint8_t const *message, size_t message_length) {
    // _olm_decode_base64(message, message_length, tmp) — clobbers `message` buffer!
}
```

**Fix:** Copy the message buffer before calling, restore after.
```cpp
std::string msgCopy(message.data(), message_length);
size_t len = olm_group_decrypt_max_plaintext_length(
    session, msgCopy.data(), msgCopy.size(), index);
// `msgCopy` is now garbage — that's OK, we just needed the length

// Use the ORIGINAL `message` buffer for the actual decrypt call:
memcpy(msgCopy.data(), message.data(), message_length);
olm_group_decrypt(session, msgCopy.data(), msgCopy.size(), plaintext.data(), len, index);
```

### Quirk 7: `olm_*_session(memory)` ZEROS the struct — call ONCE

```cpp
// olm.cpp — olm_session(void *memory) calls olm::unset(...)
// which calls olm_clear_session(...) which ZEROS the entire struct.
OlmSession *olm_session(void *memory) {
    olm::unset(OlmSession, memory);  // ← zeros the struct
    new(memory) OlmSession;          // ← placement new (constructor)
    return (OlmSession*)memory;
}

// olm_account() and olm_*_group_session() do the SAME thing.
```

```cpp
// ✅ CORRECT: call once on malloc'd memory
void* mem = malloc(olm_session_size());
OlmSession* session = olm_session(mem);

// ✅ CORRECT: use static_cast for subsequent access
auto* s = static_cast<OlmSession*>(mem);

// ❌ WRONG: calling again ZEROS the session
session = olm_session(mem);  // DESTROYS existing session!
```

### Quirk 8: `olm_unpickle_session` MUTATES pickle buffer IN-PLACE

```c
// libolm uses the pickled buffer as scratch space for decryption.
// After olm_unpickle_session(), the buffer contains GARBAGE
// (decrypted plaintext, not encrypted pickle).
size_t olm_unpickle_session(OlmSession *session,
    void const *key, size_t key_length,
    void *pickled, size_t pickled_length)
    // ^^^ NOT const — libolm will modify `pickled` in-place!
```

**Fix:** ALWAYS pass a COPY of the pickle data.
```cpp
// ✅ CORRECT
std::string copy = storedPickleData;
size_t result = olm_unpickle_session(session, key, keyLen,
    copy.data(), copy.size());
// `copy` is now garbage — that's OK

// ❌ WRONG — destroys the stored pickle
size_t result = olm_unpickle_session(session, key, keyLen,
    storedPickleData.data(), storedPickleData.size());
// storedPickleData is now garbage — next unpickle fails!
```

Same applies to `olm_unpickle_account()` and `olm_unpickle_pk_decryption()`.

### Quirk 9: `olm_account_unpublished_fallback_key_length` returns NON-ZERO for the EMPTY form

```c
// Even when NO fallback key exists (or the current one is published),
// olm_account_unpublished_fallback_key_length() returns the length of
// {"curve25519":{}} (~17 bytes) — NOT 0. (account.cpp:359-366)
```

**Fix:** Detect the empty form by CONTENT, not length — check for the `:{}` empty-map
substring, or parse and verify the inner map is empty. `generateRandomBytes` is a
CSPRNG (see fallback key docs) — never gate on length alone.

---

## Data Format Summary

```
Matrix JSON (all base64)
    │
    ▼ olm_*_inbound_session(), olm_decrypt(),
    │ olm_init_inbound_group_session(),
    │ olm_import_inbound_group_session()
    │ [internal b64_input / _olm_decode_base64]
    ▼
libolm internal (raw bytes)
    │
    ▼ olm_encrypt(), olm_group_encrypt()
    │ [internal b64_output / _olm_encode_base64]
    ▼
Matrix JSON (all base64)
```

Key rule: **NEVER add encode/decode between Matrix JSON and libolm functions.**
libolm already handles all base64 conversions internally.

---

## E2EE Recovery Chain (Bug A fix — July 2026)

When an encrypted message arrives but we have no Megolm session:

```
┌──────────────────────────────────────────────┐
│ 1. decryptMegolmEvent fails (no session)     │
│    → Save to pending-events queue            │
│    → Call requestRoomKey()                   │
├──────────────────────────────────────────────┤
│ 2. requestRoomKey → POST /sendToDevice       │
│    → m.room_key_request to sender            │
├──────────────────────────────────────────────┤
│ 3. forceNewOlmSession → if no Olm session    │
│    → Create outbound OlmSession              │
│    → Send m.dummy to-device (wakes sender)   │
├──────────────────────────────────────────────┤
│ 4. Sender receives m.dummy                   │
│    → Creates inbound OlmSession from m.dummy │
│    → Sees m.room_key_request                 │
│    → Re-shares m.room_key via fresh Olm 1:1  │
├──────────────────────────────────────────────┤
│ 5. handleOlmEncryptedToDevice                │
│    → Decrypts m.room_key                     │
│    → handleRoomKey → createInbound           │
│    → Megolm inbound session stored           │
├──────────────────────────────────────────────┤
│ 6. processPending → re-decrypt saved events  │
│    → DECRYPTED ✓                             │
└──────────────────────────────────────────────┘
```

### Critical order: m.dummy BEFORE requestRoomKey

Sending requestRoomKey via HTTP before m.dummy causes Element to respond to the key request using an OLD Olm session (if one exists). The m.dummy MUST arrive first so Element creates a fresh Olm session from it, then the key request arrives on that new session.

### Throttling

- `requestRoomKey`: one request per (room, sessionId, senderKey) per run — avoids spam
- `forceNewOlmSession`: one m.dummy per senderKey per run — avoids infinite Olm handshake loops

### Do NOT erase Olm sessions on decrypt failure

The Olm session might still be valid — the decrypt failure could be due to a missing Megolm session, not a bad Olm session. Erasing the Olm session would break future to-device messages from that sender.

---

## Megolm Plaintext Format (Bug B fix — July 2026)

Element's matrix-rust-sdk checks `room_id` in Megolm plaintext:

```json
{
  "type": "m.room.message",
  "content": {
    "msgtype": "m.text",
    "body": "Hello"
  },
  "room_id": "!roomid:server.org"
}
```

Without `room_id`, Element returns `MismatchedRoom` error and refuses to decrypt. Our plaintext was missing this field.
**Fix:** Added `"room_id":"..."` to the plaintext JSON before Megolm encryption (`chat_view.cpp`).

### Spec compliance — fields to exclude

The following are NOT in the Matrix E2EE spec for `m.room_key` content and should be removed:
- `sender_device_keys` — Element/FluffyChat ignore it
- `sender_key` in room_key content (the sender_key is in the to-device metadata, not content)

---

## Self-Echo (own messages encrypted after room switch)

When we send an encrypted message, we encrypt with our outbound Megolm session. But we also need to DECRYPT it so the message appears in our own timeline along with messages from others.

**Mechanism:** After creating/re-using an outbound Megolm session, import it as an inbound session via `megolm_->addInboundSession()`:

```cpp
std::string getOrCreateOutboundSession(const std::string& roomId) {
    if (hasOutboundSession(roomId)) return getOutboundSessionId(roomId);
    // Create new outbound session
    auto session = createOutboundSession();
    // Import as inbound for self-echo
    std::string sessionKey = getOutboundSessionKey(roomId);
    megolm_->addInboundSession(roomId, sessionKey);
    return session->sessionId;
}
```

### Send order: share key BEFORE encrypt message

When sending the first message in an encrypted room:
1. Share room key via /sendToDevice (Olm 1:1)
2. THEN send encrypted message to room timeline

If you send the message first, the recipient receives it BEFORE the to-device room_key arrives → "Unable to decrypt". Correct order ensures room_key arrives first.

---

## Token Rotation & `ctxToken_`

### The bug

`Decryptor` stored `accessToken` at E2EE init (`setCryptoContext`). But a pre-refresh (`session_bootstrap.cpp:76`) rotated the token 30 lines later. `ctxToken_` held the stale value → all `forceNewOlmSession`/`requestRoomKey` HTTP calls got 401 → Bug A recovery chain stalled.

### The workaround (current)

Re-call `setCryptoContext` after every token rotation site:
- `session_bootstrap.cpp:85` (pre-refresh)
- `sync_engine.cpp:164` (sync-triggered refresh)

### The permanent fix (deferred — Option A)

Inject `shared_ptr<MatrixClient>` into `Decryptor`, read `accessToken` LIVE from `client->account()` at call time. Eliminates the cache entirely — matching Nheko's `http::client()` pattern.

```cpp
// Current (stale-prone):
void requestRoomKey(...) {
    auto headers = makeAuthHeaders(ctxToken_);  // may be stale
    httpPost(url, body, headers, timeout);
}

// Option A (live):
void requestRoomKey(...) {
    auto headers = makeAuthHeaders(client_->account().accessToken);  // always fresh
    httpPost(url, body, headers, timeout);
}
```

---

## Olm Session Persistence

### Storage format

Multiple Olm sessions per sender curve25519 key — NOT single pickle:

```cpp
// decryptor.hpp:177-179
std::unordered_map<std::string, std::vector<std::string>> olmSessions_;
//                      curve25519key → [pickle1, pickle2, ...]
```

Multiple sessions needed because Olm sessions expire after one message in each direction. A new m.dummy creates a new session. Old sessions must be kept for pending to-device messages.

### Pickle operations

```cpp
// Save: iterate all senderKeys, pickle each session, concatenate
std::string pickleOlmSessions(const std::string& key) {
    std::string result;
    for (auto& [curve25519, sessions] : olmSessions_) {
        for (auto& session : sessions) {
            OlmSession sess;
            std::string copy = session;  // Quirk 8: pass copy!
            sess.unpickle(key, copy);
            result += sess.pickle(key) + "\n---\n";
        }
    }
    return result;
}
```

### Multi-device OTK bug

`shareRoomKey` currently claims only 1 OTK per device — but if a friend has 2+ devices (FluffyChat on phone + Element on desktop), one device gets the room_key, the other doesn't. Fix: claim enough OTKs for ALL device keys from `/keys/query`.

---

## Common Mistakes (lessons from 27 E2EE sessions)

| # | Mistake | Symptom | Fix |
|---|---|---|---|
| 1 | `base64Decode()` before any `olm_*` function | Corrupted ciphertext, decrypt fails | Don't decode — libolm does it internally (Quirks 1-5) |
| 2 | Re-calling `olm_*_session(memory)` | Session destroyed (zeroed) | `static_cast<OlmSession*>(memory)` instead (Quirk 7) |
| 3 | Passing pickle data directly to `olm_unpickle_*` | Pickle corrupted, all subsequent unpickles fail | Pass a COPY (Quirk 8) |
| 4 | Not restoring message buffer after `max_plaintext_length` | Decrypt fails with corrupted input | `memcpy`-restore (Quirk 6) |
| 5 | Erasing Olm session on decrypt failure | Recovery chain stalls (no session to receive re-shared key) | Keep session, try recovery instead |
| 6 | Sending room_key BEFORE encrypting message | Recipient gets message before key → "Unable to decrypt" | Share key first, then message |
| 7 | Missing `room_id` in Megolm plaintext | Element/rust-sdk: `MismatchedRoom` error | Include `room_id` in plaintext JSON |
| 8 | Caching access token in member variable | 401 on token rotation → recovery chain stalls | Read live from `MatrixClient::account()` |
| 9 | Only claiming 1 OTK for multi-device friends | One device gets key, other doesn't → can't decrypt | Claim N OTKs where N = device count |
| 10 | Not importing outbound as inbound | Own messages stay encrypted after room switch | `addInboundSession(self.sessionKey)` |

---

## Spec Compliance — What We Follow

| Spec section | Status | Notes |
|---|---|---|
| m.olm.v1.curve25519-aes-sha2 | ✅ Working | Olm 1:1 encryption |
| m.megolm.v1.aes-sha2 | ✅ Working | Group message encryption |
| m.room_key | ✅ Working | Megolm session sharing |
| m.room_key_request (send) | ✅ Working | Recovery chain |
| m.room_key_request (receive) | ❌ | Don't re-share keys when asked |
| m.dummy | ✅ Working | Force Olm handshake |
| m.room.encrypted (room) | ✅ Working | Megolm encrypted events |
| m.room.encrypted (to-device) | ✅ Working | Olm encrypted to-device |
| device_keys / one-time_keys | ✅ Working | /keys/upload |
| Olm session persistence | ✅ Working | Multiple sessions per sender, dedup, cap 20/sender |
| Megolm inbound persistence | ✅ Working | SQLite, scoped per-account |
| Megolm outbound persistence | ✅ Working | SQLite, scoped per-account, roomKeysShared flag restored |
| Ed25519 signature verification | ✅ Working | `olm_ed25519_verify` (PUBLIC libolm API) — replaces submodule stub |
| Olm plaintext validation | ✅ Working | sender/recipient/recipient_keys per m.olm.v1 |
| OTK signature verification | ✅ Working | signed_curve25519 verified on /keys/claim |
| Device key signature verification | ✅ Working | device_keys signatures verified on /keys/query |
| Device verification (SAS) | ✅ Working | Phase 2 — m.sas.v1 state machine + crypto + dialog + two-manager protocol test green |
| Cross-signing | ❌ | No device trust chain (Phase 6, depends on ed25519 verify ✅) |
| SSSS key backup | ❌ | No history recovery (Phase 7) |
| m.forwarded_room_key | ❌ | Don't handle forwarded keys (submodule has real code, ready to port) |
| m.room_key_request incoming | ❌ | Don't re-share keys when asked (submodule has real code, ready to port) |
| Megolm rotation | ❌ | No forward secrecy (submodule has real code, ready to port) |
| Fallback keys | ❌ | API available (submodule OlmAccountData), needs port to OlmAccount class |

---

## Testing Matrix

| What | How to verify |
|---|---|
| Inbound decrypt | Have Element/FluffyChat send encrypted message → appears decrypted |
| Outbound decrypt | Send encrypted message → appears decrypted in Element/FluffyChat |
| Self-echo | Send message → switch rooms → come back → message is decrypted |
| Recovery chain | Start new room, send from Element → our client decrypts (m.dummy + room_key_request) |
| Token rotation | Wait for M_UNKNOWN_TOKEN → recovery still works (fresh token used) |
| Pickle persistence | Close + reopen → Olm sessions survive, decrypt still works |
| Multi-device | Login on 2 devices → both receive room_key, both decrypt |
| **Cross-account E2EE round-trip** | **Automated — `tests/test_synapse_e2ee.cpp` (live Synapse in CI): registers alice+bob, creates encrypted room, shares room key, bob decrypts alice's message. Skips gracefully when no server.** |
| SAS protocol | **Automated — `tests/test_e2ee_verify_protocol.cpp`: two managers complete request→done, emoji match, corrupted-MAC cancel.** |

---

## Multi-Account E2EE Architecture

### The Problem: Why Multi-Account Broke E2EE

Progressive Chat supports multiple Matrix accounts within a single app instance. Each account has its own Olm identity (curve25519/ed25519 key pair), its own Megolm sessions, and its own Olm 1:1 sessions.

The original implementation stored all E2EE state in a **shared SQLite database** with **global keys** — not scoped per account. This caused 5 compounding bugs:

1. **`olm_account` single-row table** (`id INTEGER PRIMARY KEY CHECK (id=1)`) — only ONE Olm account pickle could exist. Switching accounts overwrote the previous account's pickle with the new one.

2. **`otk_uploaded_once` global flag** — stored in shared `e2ee_data` table, not scoped per userId/deviceId. Account A uploading OTKs set the flag → Account B saw "already uploaded" → skipped upload → B's device keys never reached the server.

3. **`e2ee_data['megolm']` single key** — ALL accounts shared one Megolm session store. Account A's sessions overwrote Account B's.

4. **`e2ee_data['olm_sessions']` single key** — same bug as Megolm. Account A's Olm sessions overwrote Account B's.

5. **Double-init in account switcher** — `switchAccount()` called `init()` (creates new Olm account) then `init(pickle, key)` (tries to load pickle). If load failed, `create()` was called AGAIN → libolm quirk #7: re-calling zeros the struct → curve25519 = "AAAA..." (all zeros). Uploaded garbage keys to server.

### The Fix: Per-Account Scoping

All fixes were applied July 28-29, 2026 (10+ commits):

| Bug | Fix | Pattern |
|---|---|---|
| `olm_account` single-row | Multi-row table, keyed by `pickle_key` (userId/deviceId) | FluffyChat's per-account database (approximated via scoped keys) |
| Global `otk_uploaded_once` | Replaced with `shared` flag on OlmAccountStore (matches matrix-rust-sdk `Account.shared`) | matrix-rust-sdk pattern |
| Global `megolm` / `olm_sessions` | Scoped to `megolm:userId/devId` / `olm_sessions:userId/devId` | Same pattern as olm_account table |
| Double-init | Removed `init()` pre-check, call `init(pickle, key, shared)` directly | No double-create |

### The `shared` Flag Pattern (matrix-rust-sdk)

```cpp
// OlmAccountStore (olm_account.hpp)
class OlmAccountStore {
    bool shared_ = false;           // has device_keys been uploaded?
    int uploadedKeyCount_ = 0;      // OTKs on server (from /sync)

    bool shared() const { return shared_; }
    void markAsShared() { shared_ = true; }
    int uploadedKeyCount() const { return uploadedKeyCount_; }
    void setUploadedKeyCount(int c) { uploadedKeyCount_ = c; }
};

// sync_engine.cpp:uploadDeviceKeys()
bool needDeviceKeys = !decryptor_.accountShared();
// shared=false → upload device_keys + OTKs → markAsShared → save pickle
// shared=true  → skip device_keys, OTK managed by auto-refresh
```

**How it matches matrix-rust-sdk**:
```rust
// matrix-rust-sdk account.rs:693
pub fn keys_for_upload(&self) -> (Option<DeviceKeys>, ...) {
    let device_keys = self.shared().not().then(|| self.device_keys());
    // shared=false → Some(device_keys) → upload
    // shared=true  → None → skip
}
```

The `shared` flag is **on the Olm account itself** (stored in `olm_account.shared` column), NOT in a global DB flag. New account → `shared=false` → upload. Loaded from pickle → `shared` restored from DB. After successful upload → `shared=true` → persisted.

### OTK Count Tracking (matrix-rust-sdk pattern)

```cpp
// sync_engine.cpp — update from every /sync response
if (result.data.signedCurve25519Count > 0) {
    decryptor_.account()->setUploadedKeyCount(result.data.signedCurve25519Count);
}

// Smart generation: only generate what's needed
int serverCount = decryptor_.account()->uploadedKeyCount();
int needed = std::max(0, 100 - serverCount);  // 100 = libolm MAX_ONE_TIME_KEYS
if (needed == 0 && shared && !needDeviceKeys) {
    return;  // server has enough, nothing to do
}
// discard old unpublished OTKs, generate exactly `needed`
decryptor_.markOneTimeKeysPublished();
std::string body = decryptor_.buildKeysUploadBody(userId, deviceId, needed, needDeviceKeys);
```

**How it matches matrix-rust-sdk**:
```rust
// account.rs — generate_one_time_keys_if_needed()
let count = self.uploaded_key_count();
if count >= max_keys as u64 { return None; }  // enough → skip
let key_count = (max_keys as u64) - count;     // only generate needed
```

### device_lists Tracking (matrix-rust-sdk pattern)

The `/sync` response includes `device_lists: {changed: [...], left: [...]}` — the server tells us when other users' device keys change. We now parse this in `fast_sync.cpp` and mark users as "stale" in `Decryptor::staleDeviceUsers_`. On next `shareRoomKey` call, we LOG stale users (future: re-query /keys/query).

```cpp
// fast_sync.cpp — parse device_lists
auto deviceLists = root["device_lists"].get_object();
// Parse "changed" array → resp.deviceListChanged
// Parse "left" array → resp.deviceListLeft
LOG("device_lists: changed=%zu left=%zu", ...);

// sync_engine.cpp — mark stale
if (!result.data.deviceListChanged.empty()) {
    decryptor_.markDevicesStale(result.data.deviceListChanged);
}
```

### The 167 Stale OTKs Story (A Cautionary Tale)

**What happened**: During the broken period (July 22-28), each account switch created a NEW Olm account (new curve25519) due to the single-row `olm_account` table. Each new account uploaded 10 fresh OTKs. But the old OTKs from previous accounts stayed on the server (the Matrix spec says `/keys/upload` ADDS OTKs, doesn't replace). After multiple switches, 167 OTKs accumulated on the server — most from OLD Olm accounts, now orphaned (wrong key pair — can't be used with current curve25519).

**The symptom**: FluffyChat/Element would query `/keys/query` for our device → get our current curve25519 → claim an OTK from the server → receive an ORPHANED OTK (from an old Olm account) → Olm session creation FAILS (wrong key pair) → give up → never share room_key → we can't decrypt their messages.

**Our outbound worked** because WE queried THEIR fresh device keys → claimed THEIR fresh OTKs → Olm session succeeded → room_key shared correctly.

**The fix**: "Reset device keys" action (Settings menu). Calls `POST /_matrix/client/v3/delete_devices` with password auth → deletes the device from the server (clears ALL stale OTKs) → restart → `shared=false` → re-uploads fresh device_keys + fresh OTKs → server has ONLY fresh keys → other clients claim fresh OTKs → Olm sessions work.

### Per-Account Session Persistence

Megolm and Olm sessions are stored in `e2ee_data` KV table with scoped keys:

```sql
-- Old (broken): ALL accounts shared one row
SELECT value FROM e2ee_data WHERE key='megolm';

-- New (fixed): each account has its own row
SELECT value FROM e2ee_data WHERE key='megolm:@t1s3:matrix.org/pd-745a8faa...';
SELECT value FROM e2ee_data WHERE key='megolm:@t0n1a:matrix.org/pd-745a8faa...';
```

The pickleKey (userId/deviceId) is used both for XOR encryption of the pickle data AND as part of the DB key. This prevents cross-contamination when switching accounts.

### Megolm Pickle Format (XOR + Hex)

```cpp
// Saved: JSON array → XOR with pickleKey → hex encode → e2ee_data
std::string megolmPickle = pickleAll(pickleKey);
store->saveMegolmSessions(megolmPickle, pickleKey);

// Loaded: hex decode → XOR with pickleKey → JSON parse → unpickle
auto data = store->loadMegolmSessions(pickleKey);
megolm_->unpickleAll(pickleKey, *data);
```

**Critical**: `sessionCount()` inside `unpickleAll()` holds `mtx_` (line 108). The bonus diagnostic LOG at line 159 called `sessionCount()` which tries to lock `mtx_` again → non-recursive mutex → SIGABRT. Fixed by using `impl_->mgr.sessionCount()` (direct access, no lock needed while already locked).

### Multi-Device Handling (1 user, 2+ devices)

When a user has multiple devices (e.g., Progressive on PineTab + Element on phone):

1. `/keys/query` returns ALL devices for the user (different device_ids, different curve25519 keys)
2. `shareRoomKey` queries `/keys/query` for all room members, finds ALL devices
3. Claims 1 OTK per device via `/keys/claim` (single request, all devices)
4. Creates an Olm session with EACH device
5. Encrypts room_key for EACH device separately
6. Sends via `/sendToDevice`

**Known issue**: FluffyChat multi-device OTK claim — if a friend has 2+ devices and one has no available OTKs, the server returns nothing for that device → we skip it → that device doesn't get the room_key. FluffyChat's workaround: re-upload OTKs regularly.

### Phase 1 — Signature Verification Foundation (Complete, July 30, 2026)

Three new capabilities that form the cryptographic foundation for all future E2EE features:

**Ed25519 Signature Verification (`olm_ed25519_verify`):**
libolm exports `olm_ed25519_verify` as a PUBLIC stable API (`olm.h:516`):
```c
size_t olm_ed25519_verify(OlmUtility*, void const* key, size_t keyLen,
    void const* message, size_t msgLen, void* signature, size_t sigLen);
```
- All inputs are base64 (key, signature). Message is raw bytes. Returns 0 on success.
- The progressive submodule's `ed25519Verify` was a STUB returning `true`. Replaced with real libolm call.
- The foundation for: OTK sig verify, device key sig verify, cross-signing signature verification, SAS MAC verification.
- Implementation: `src/core/crypto/ed25519.hpp` (free functions `ed25519Verify` / `ed25519VerifyJson`).

**Olm Plaintext Validation (per m.olm.v1 spec):**
After Olm decryption succeeds, verify four fields in the plaintext JSON:
- `sender` = the to-device event sender
- `recipient` = our userId
- `recipient_keys.ed25519` = our ed25519 key
- `keys.ed25519` = sender's device ed25519 (logged, not enforced — needs device key cache)

Implementation: `decryptor.cpp:handleOlmEncryptedToDevice` — validation before `handleRoomKey`.

**OTK + Device Key Signature Verification:**
- Device key signatures verified on `/keys/query` responses (canonical JSON ← buildDeviceKeysCanonical)
- OTK signatures verified on `/keys/claim` responses (canonical JSON = `{"key":"<curve25519>"}`)
- Both use the `ed25519Verify` primitive
- Untrusted devices/OTKs are SKIPPED (logged, not crashed)
- Implementation: `src/core/crypto/sig_verify.hpp` (free functions), wired into `shareRoomKey`

### Phase 2 — SAS Verification (Complete, August 1, 2026)

Full `m.sas.v1` device verification (the "make the red shield go away" feature).

**Ported from submodule (REAL):**
- `sas_verification.cpp` (212L) — OlmSAS wrapper: sasCreate, sasSetTheirKey, sasGetEmojis, sasCalculateMac, sasCalculateMacLongKdf, sasVerifyMac. 13 `olm_sas_*` calls → now `sas.cpp`/`sas.hpp` (127L).
- `verification_utils.cpp` (156L) — 64-emoji table (spec-compliant), computeSasEmojis, computeSasDecimals, message builders → now `sas_emojis.cpp` (91L) + `verification.cpp`.

**Written from scratch (submodule's sas_manager.cpp is FAKE):**
- `VerificationManager` (`verification.hpp`, 119L) — state machine (Idle→RequestSent→RequestReceived→Ready→Started→Accepted→KeySent/Received→MacSent/Received→Done/Cancelled), transaction tracking, commitment hash verification, MAC verification, `StateChangedFn` callback fired after each transition
- `VerificationController` (`verify_controller.cpp`, 162L) — send side + UI coordination, wires sendToDeviceFn + DeviceKeyResolverFn (/keys/query)
- `SasVerificationDialog` (`sas_verification_dialog.cpp`, 70L) — Qt dialog: 7 emoji display, "They match"/"They don't match" buttons, cancel/timeout
- `VerificationHandler` (`verification_handler.cpp`, 151L) — drives the UI from state changes, accept banner, emoji dialog, cancel codes
- Entry points: RoomMembersDialog right-click → "Verify…", PrefsDialog → "Your devices" → Verify
- To-device + in-room event routing for `m.key.verification.*`
- **Two-manager protocol test** (`tests/test_e2ee_verify_protocol.cpp`, 214L) — full request→done flow with emoji match + corrupted-MAC cancel path, both managers in-process

**Spec-compliance bugs found & fixed (Aug 1, all verified by the two-manager test):**
1. SasSession move semantics — `olm_sas_set_their_key` decodes base64 IN-PLACE (libolm quirk #9) → pass a copy (`a91ca63`)
2. MAC info string — 7 parts, no separators, per-key `keyId` not `keyValue` (`ad493cc`, `04a61ce`)
3. computeSasDecimals — 7 values from overlapping 13-bit windows (was 2 from non-overlapping 3-byte) (`37c265a`, `ad493cc`)
4. Mutex deadlock — handleEvent re-locked non-recursive mutex via findTransaction → added `findTransactionLocked` (`2732bf9`)
5. MAC verified before Done transition + `m.key.verification.done` sent (`f5aeb27`)
6. Commitment computed in start handler + verified in key handler (`d8ee118`)
7. In-room routing wired + `from_device` on accept/key/mac (spec requires it) (`1f05fc6`, `3c804b1`)
8. Initiator stores own start JSON for commitment verification (`6257837`)
9. ourDeviceId/ourUserId/ourEd25519/ourCurve25519 stored on transactions; ourDeviceId param to handleEvent (`886a930`, `91ebfd0`)
10. verifyTheirMac over THEIR keys — theirEd25519/theirCurve25519 + DeviceKeyResolverFn via /keys/query (`a3d6a26`)
11. buildSasInfo role inversion — weInitiated means accepter (start*=other*) (`48f57bf`)
12. key_agreement_protocol `curve25519-hkdf-sha256` — libolm always applies HKDF-SHA256 (`ea60ddc`)
13. Emoji table 64th entry (Hammer) — index 63 silently dropped, intermittent 6-emoji mismatch vs Element (`e27d555`)
14. CancelCode-aware cancelVerification — Not Match sends `m.mismatched_sas` not `m.user` (`e50186c`)
15. `olm_sas_calculate_mac` resize crash — ret=0 on success, use macLen (`ed68f44`)
16. SAS pubkey base64 double-encode/pre-decode — libolm handles base64 internally (`12e542e`)
17. m.key_mismatch cancel on commitment + MAC mismatch — other party no longer waits 10min timeout (`8ffd0b0`)

### FAKE Boilerplate Trap Warning — DO NOT PORT THESE FILES

The progressive submodule has ~30+ FAKE files that look like implementations but are auto-generated JSON-echo no-ops. A filename-and-line-count audit badly overstates what's there.

**FAKE files (DO NOT PORT):**
- All `*_v4.cpp` files (~220 KB total): `verification_v4.cpp`, `cross_sign_v4.cpp`, `key_backup_v4.cpp`, `secret_store_v4.cpp`, etc.
- `crypto_ops.cpp` (34 KB)
- `sas_manager.cpp`, `backup_controller.cpp`, `gossip_manager.cpp`
- Most `*_utils.cpp`, `*_manager.cpp`, `backup_*`, `cross_sign_*`, `device_*`
- `verification_request_utils.cpp`, `key_export_utils.cpp`, `dehydrate_utils.cpp`
- `secret_*` files

**REAL files (SAFE to port):** `sas_verification.cpp`, `verification_utils.cpp` (1 known bug), `keyshare.cpp`, `room_encryption.cpp`, `key_backup.cpp` (recovery key format only — not backup crypto), `cross_signing_manager.cpp` (data model only — not crypto), `crypto_algorithms.cpp`, `olm_session.cpp:157` (fallback key on OlmAccountData API), `megolm_decryptor.cpp`.

### Fallback Keys — NOT Blocked on Submodule

Previously marked "blocked on submodule" (P4). The submodule HAS a real implementation — but on the `OlmAccountData` API in `olm_session.cpp:157`, not on the `OlmAccount` class the desktop wraps. `device_unused_fallback_key_types` from /sync parsing also exists (`sync_models.cpp`). The remaining work is an API alignment/port: add `generateFallbackKey` + `fallbackKey()` getter + `markFallbackKeyPublished()` to the `OlmAccount` class (or migrate desktop to `OlmAccountData`). This is a porting task, not a "write from scratch" task.

### Architecture Comparison — Multi-Account E2EE

| Aspect | matrix-rust-sdk | FluffyChat | Progressive Chat |
|---|---|---|---|
| Account storage | Per-account crypto store | Per-account SQLite DB | Shared SQLite, scoped keys |
| `shared` flag | On Account struct, pickled | Implicit (pickle exists) | On OlmAccountStore, DB column |
| OTK count tracking | `uploaded_signed_key_count` from /sync | `handleDeviceOneTimeKeysCount()` | `uploadedKeyCount_` from /sync |
| OTK generation | `generate_one_time_keys_if_needed()` — only if needed | `(2/3*max - oldCount)` | `max(0, 100 - serverCount)` |
| Fallback keys | ✅ Weekly rotation | ✅ Generated if no unused | ❌ Blocked on submodule (P4) |
| `device_lists` tracking | ✅ Re-queries changed users | ✅ Cache invalidation | ✅ Marks stale, LOG only |
| Device reset | Delete device API | Re-login with new device_id | "Reset device keys" action |
| Megolm persistence | Per-account crypto store | Per-account DB | Scoped `e2ee_data` keys |
| Olm session GC | LRU with TTL | LRU with TTL | ❌ No GC (DEBT comment) |

## Device Reset — Clearing Stale OTKs

One-time operation accessible from Settings menu. Requires password authentication.

1. User clicks "Reset device keys" → warning dialog → enter password
2. `POST /_matrix/client/v3/delete_devices {"devices":["pd-745a8faa..."],"auth":{"type":"m.login.password","user":"@user","password":"..."}}`
3. Server deletes the device → ALL stale OTKs cleared
4. On next app start (or manual restart): `shared=false` → uploads fresh device_keys + fresh OTKs
5. Other clients re-query `/keys/query` → get fresh curve25519 + fresh OTKs
6. E2EE works correctly in both directions

## File Reference

| File | Purpose |
|---|---|
| `src/core/crypto/decryptor.cpp` | E2EE coordinator (1430 lines) |
| `src/core/crypto/decryptor.hpp` | Decryptor class — `shared()`, `markDevicesStale()`, `isDeviceStale()` |
| `src/core/crypto/olm_account.cpp` | OlmAccountStore — `shared_`, `uploadedKeyCount_`, `markAsShared()` |
| `src/core/crypto/megolm_store.cpp` | MegolmStore — pickleAll + unpickleAll with XOR + hex |
| `src/core/crypto/event_body_parser.hpp` | `parsePlaintextBody()` — shared parser |
| `src/core/session_store.cpp` | SQLite: `olm_account` (pickle_key PK, shared, uploaded_key_count), `e2ee_data` (scoped keys) |
| `src/core/sync_engine.cpp` | `uploadDeviceKeys()` — shared flag check, OTK count tracking, auto-refresh |
| `src/core/fast_sync.cpp` | Parses `device_lists:{changed,left}` from /sync |
| `src/ui/handlers/e2ee_init_handler.cpp` | E2EE init: load Olm pickle + sessions, restore count |
| `src/ui/handlers/account_switcher.cpp` | Account switch: save/Load Olm pickle + megolm/olm sessions per account |
| `src/ui/handlers/toolbar_handler.cpp` | "Reset device keys" action → `deleteDevice()` API |
| `src/ui/chat/chat_view.cpp` | `doSend` — encrypts + sends with room_id in Megolm plaintext |
| `src/core/crypto/sas.cpp` | OlmSAS wrapper — ported from submodule `sas_verification.cpp` |
| `src/core/crypto/sas_emojis.cpp` | 64-emoji table + computeSasEmojis + computeSasDecimals (7 overlapping 13-bit windows) |
| `src/core/crypto/verification.cpp` | `m.sas.v1` state machine + message builders (VerificationManager, 476L) |
| `src/core/crypto/verify_controller.cpp` | SAS send side + UI coordination (wires sendToDeviceFn + DeviceKeyResolverFn) |
| `src/ui/dialogs/sas_verification_dialog.cpp` | 7-emoji comparison dialog (match/mismatch/cancel) |
| `src/ui/handlers/verification_handler.cpp` | Drives verification UI from state changes |
| `tests/test_e2ee_sas.cpp` | SAS crypto roundtrip (pubkey base64, emojis/decimals, MAC verify) |
| `tests/test_e2ee_verify_protocol.cpp` | Two-manager SAS protocol test (full flow + corrupted-MAC cancel) |
| `tests/test_synapse_e2ee.cpp` | Live-Synapse cross-account E2EE integration test (CI) |
| `third_party/progressive-android-experiments/.../olm.hpp` | `OlmAccount` class — libolm wrapper |
| `build/_deps/olm-src/src/` | libolm C source — verify quirks here before adding encode/decode |

---

*This document is intended for publication when the project reaches alpha. All quirks are verified from libolm C source. Share freely with anyone implementing Matrix E2EE with libolm.*
