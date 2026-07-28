# docs/E2EE.md — End-to-End Encryption Implementation Notes

> **Who this is for:** Developers implementing Matrix E2EE with libolm in any language.
> Written from hard-won experience implementing E2EE from scratch in a pure C++20 Matrix client.
> **Last updated:** July 27, 2026 — 15+ commits, 8 libolm quirks discovered.

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

## libolm Quirks — 8 Critical Data-Format Rules

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
| Olm session persistence | ✅ Working | Multiple sessions per sender |
| Megolm inbound persistence | ✅ Working | SQLite |
| Megolm outbound persistence | ❌ | Sessions lost on restart |
| Device verification (SAS) | ❌ | Red shield in Element |
| Cross-signing | ❌ | No device trust chain |
| SSSS key backup | ❌ | No history recovery |
| m.forwarded_room_key | ❌ | Don't handle forwarded keys |
| Megolm rotation | ❌ | No forward secrecy |

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

---

## File Reference

| File | Purpose |
|---|---|
| `src/core/crypto/decryptor.cpp` | E2EE coordinator (1190 lines) |
| `src/core/crypto/decryptor.hpp` | Decryptor class (206 lines) |
| `src/core/crypto/olm_account.cpp` | OlmAccountStore — device keys, OTK management |
| `src/core/crypto/megolm_store.cpp` | MegolmStore — inbound session persistence (SQLite) |
| `src/core/crypto/event_body_parser.hpp` | `parsePlaintextBody()` — shared parser |
| `src/ui/handlers/e2ee_init_handler.cpp` | E2EE init at login — `setCryptoContext` call |
| `src/ui/handlers/session_bootstrap.cpp` | Token pre-refresh + `setCryptoContext` re-call |
| `src/core/sync_engine.cpp` | Sync loop + token refresh + `setCryptoContext` re-call |
| `src/ui/chat/chat_view.cpp` | `doSend` — encrypts + sends, builds Megolm plaintext with room_id |
| `third_party/progressive-android-experiments/.../olm.cpp` | OlmSession C++ wrappers (pickle, unpickle, encrypt, decrypt) |
| `build/_deps/olm-src/src/` | libolm C source — verify quirks here before adding encode/decode |

---

*This document is intended for publication when the project reaches alpha. All quirks are verified from libolm C source. Share freely with anyone implementing Matrix E2EE with libolm.*
