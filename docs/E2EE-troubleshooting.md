# docs/E2EE-troubleshooting.md — E2EE Diagnostic Guide

> **Who this is for:** Developers debugging E2EE issues in Progressive Chat.
> Quick reference: grep the logs for the symptom → read the cause → apply the fix.
> **Last updated:** August 1, 2026

---

## Quick Symptom → Root Cause Index

### "[encrypted]" messages after restart

```
[e2ee] loaded megolm sessions: 0
[e2ee] persistCrypto: saved 1 megolm sessions
```

**Root cause**: Megolm sessions not persisted (saved 1, loaded 0).

**Check**: 
```bash
grep "loaded.*sessions\|persistCrypto.*saved" logs.txt
```

**Fixes** (in order of likelihood):
1. Pickle key mismatch — save uses `userId/deviceId`, but load uses different key. Both must match.
2. Single-key `e2ee_data['megolm']` — ALL accounts shared one row. Fixed by scoping keys.
3. Hex decode failed — odd-length hex string → raw data = garbage → XOR = garbage → JSON parse fails silently. `unpickleAll` returns `true` even on parse failure.

**Verify fixed**: After per-account scoping, `loaded N megolm sessions` where N > 0.

---

### "Olm: our key not found in ciphertext"

```
[E2EE] Olm: our curve25519=/OVaWlV/636mXkICJp3M...
[E2EE] Olm: ciphertext has key=BO8OAlWoJs3hWig816HbUUlcno1hx+rHN66JGamduG4
```

**Root cause**: Other client (Element/FluffyChat) has a STALE device key for us. They're encrypting Olm to-device for an old curve25519 that we no longer have.

**Check**:
```bash
grep "Olm: our curve25519\|Olm: ciphertext has key" logs.txt
```

**Causes** (in order of likelihood):
1. **167 stale OTKs** — orphaned OTKs from old Olm accounts on the server. /keys/claim returns a stale OTK → Olm session creation fails → other client gives up.
2. **Stale device key cache** — other client cached our old curve25519 from a previous /keys/query. Our new /keys/upload didn't trigger `device_lists:changed` in their /sync.
3. **OTK upload failed** (400 "already exists") — duplicate OTK IDs → no fresh OTKs on server → other client can't create Olm session.

**Fix**: "Reset device keys" in Settings → deletes device from server (clears ALL stale OTKs) → re-uploads fresh device_keys + OTKs. Then restart other clients (or wait for `device_lists:changed`).

**Verify**: After reset, `grep "Olm: our key not found"` returns nothing. `grep "handleRoomKey OK"` shows successful Olm decryption.

---

### 400 "One time key already exists"

```
[E2EE] uploadDeviceKeys: FAILED — error=One time key signed_curve25519:AAAACg already exists
```

**Root cause**: Duplicate OTK IDs. libolm generates sequential OTK IDs (AAAACg, AAAACQ, ...). After restart, it starts from the same sequence → duplicate → server rejects.

**Check**:
```bash
grep "400\|already exists" logs.txt
```

**Cause sequence**:
1. Generate 10 OTKs → upload → `markOneTimeKeysPublished()` NOT called (e.g., 401 token race)
2. Restart → old OTKs still in libolm's list (not discarded) → generate 10 MORE → 20 total
3. Upload → some IDs already on server → 400
4. Each restart accumulates more → eventually hits MAX_ONE_TIME_KEYS=100

**Fix**: Call `markOneTimeKeysPublished()` BEFORE generating new OTKs (discards old unpublished OTKs). Use OTK count from /sync to only generate `max(0, 100 - serverCount)` new OTKs. Skip upload entirely when count is sufficient and `shared=true`.

**Verify**: `grep "OTKs sufficient"` shows skip message. No 400 errors in logs.

---

### "corrupted size vs. prev_size while consolidating" CRASH

```
[CRASH] Signal 6 (SIGABRT)
[BACKTRACE] #7 MegolmStore::sessionCount()
            #8 MegolmStore::unpickleAll()
```

**Root cause**: Recursive mutex lock on `std::mutex` (non-recursive). `unpickleAll()` holds `mtx_` (line 108), then calls `sessionCount()` which tries to lock `mtx_` again (line 164) → deadlock → SIGABRT.

**Cause**: Bonus diagnostic LOG added inside `unpickleAll` called `sessionCount()` (which locks). `sessionCount()` wraps its body in `std::lock_guard<std::mutex> lk(mtx_)` — but we already hold the lock.

**Fix**: Replace `sessionCount()` with `impl_->mgr.sessionCount()` — direct access, no lock needed (already holding it).

**Verify**: No SIGABRT in backtrace with MegolmStore.

---

### curve25519 = "AAAA..." (all zeros)

```
[E2EE] uploadDeviceKeys: our curve25519=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
```

**Root cause**: libolm quirk #7 — `create()` zeros the struct when called on an already-created account. Double-init in `switchAccount()`: `init()` (creates new account) then `init(pickle, key)` (fails to load → falls back to `create()` → zeros).

**Check**:
```bash
grep "curve25519=AAAA" logs.txt
```

**Fix**: Remove the `init()` pre-check. Call `init(pickle, key, shared)` directly — it handles both load-from-pickle and create-new cases.

**Verify**: `grep "our curve25519="` shows a real 43-char base64 key (not all A's).

---

### "no device found for senderKey" during forceNewOlmSession

```
[e2ee] forceNewOlmSession: no device found for senderKey=/OVaWlV/636mXkICJp3M
```

**Root cause**: Server has no device with this curve25519. Either: (a) we never uploaded device_keys (stale `otk_uploaded_once` flag), or (b) this is our OWN key and we're trying to find ourselves in /keys/query (self-request).

**Check context**: If senderKey matches `our curve25519=` → self-request (our other device sent the message). Normal — not a bug. If different key → our device_keys not on server.

---

### Account switch: megolm sessions lost (loaded 0)

```
[mem] before-account-switch: ...
[e2ee] loaded megolm sessions: 0
```

**Root cause**: `e2ee_data['megolm']` single key — account switch overwrites data. Account A's sessions saved → account B's sessions overwrite them → switch back → B's data loaded with A's pickle key → unpickle fails.

**Fix**: Per-account scoping — `e2ee_data['megolm:userId/devId']`. Each account has its own key.

**Verify**: Switch account A → B → A. Both should show `loaded N megolm sessions` where N > 0.

---

### SIGSEGV on app close (Signal 11)

```
[CRASH] Signal 11 (SIGSEGV)
[BACKTRACE] #3 OlmAccountStore::setUploadedKeyCount+0x14
            #4 SyncEngine::run()+0xf6c
```

**Root cause**: Detached sync worker thread → use-after-free. On close, `sync_.stop()` sets `running_=false` and calls `worker_.detach()` — the thread keeps running. `~MainWindow()` → `~SyncEngine()` destroys `decryptor_` (member by value). The detached thread calls `decryptor_.account()->setUploadedKeyCount(167)` → writes to freed memory → SIGSEGV.

**Fix**: Change `worker_.detach()` to `worker_.join()` in `sync_engine.cpp stop()`. The sync timeout is now 3000ms (configurable), so join() waits at most 3s. Acceptable on close and account switch. Also add `if (!running_) break;` defense-in-depth checks before decryptor_ accesses.

**Verify**: Open app, sync, CLOSE app → must NOT crash. Previously crashed every close.

---

### App freezes / hangs on startup — "loaded session" repeating forever

```
[e2ee] olm: loaded session CpZiYQSb1EfmyJU9yvW5CiI9N69jPs (pickleLen=352)
[e2ee] olm: loaded session CpZiYQSb1EfmyJU9yvW5CiI9N69jPs (pickleLen=352)
... (30000+ repeats, app hangs for minutes, Ctrl+C does nothing)
```

**Root cause**: Exponential Olm session growth. `switchAccount` saves old sessions to DB, then loads new sessions via `unpickleOlmSessions` which APPENDS to the map WITHOUT clearing first. Each switch compounds: A→B saves A, loads B on top of A → map has A+B. B→A: saves (A+B), loads A on top of (A+B) → map has 2A+B. Exponential growth. After ~10 switches: 30000+ sessions. Loading them takes minutes (each with a LOG line).

Also causes: SIGABRT (SIGABRT) on string allocation (21 MB JSON from 30000 pickles), 250 MB startup RAM, 5-minute startup hang.

**Check**:
```bash
grep "loaded.*olm session pickles" logs.txt
```
If count > 1000 → growth bug.

**Fix**: (1) Add `olmSessions_.clear()` at start of `unpickleOlmSessions`. (2) Add dedup check before push_back. (3) Cap at 20 sessions per senderKey. (4) Same fix for `unpickleOutboundSessions` (also appends without clear). (5) Auto-cleanup: if loaded data > 500 KB, re-save trimmed version.

**Verify**: After fix + restart: `grep "loaded.*olm session pickles"` shows ~30-60, NOT 30000. RAM < 150 MB. Startup < 5 seconds. Switch accounts 5x → count stays stable.

---

### Ctrl+Tab logs out both accounts

```
User presses Ctrl+Tab → account switches → both accounts gone → re-login required
```

**Root cause**: Ctrl+Tab cycles through ALL combo box indices, not just account indices. With 2 accounts the combo is: [0=acct1, 1=acct2, 2=separator, 3=+Add Account, 4=Logout]. From index 1, next=2 → Qt skips separator → lands on 3 (+Add Account) or 4 (Logout). Index 4 → `auth_->logout()` → `clearAccount()` (DELETE FROM account with NO WHERE) → both accounts nuked.

**Fix**: (1) Ctrl+Tab: only cycle `0..accountCount-1`, skip separator/special items. Use `accountCombo_->blockSignals(true)` + `switchAccount(next)` directly. (2) `clearAccount()`: use `DELETE FROM account WHERE user_id=?` instead of no-WHERE.

**Verify**: With 2 accounts, press Ctrl+Tab repeatedly → must only cycle between account 1 and account 2. Must NEVER trigger logout or add-account.

---

### SasSession double-free CRASH (Phase 2 SAS)

```
[CRASH] Signal 6 (SIGABRT) — double-free
[BACKTRACE] SasSession::~SasSession() → free(sas)
```

**Root cause**: `SasSession` has `void* sas` + destructor that calls `free(sas)`, but NO move constructor/assignment. When `sasCreate()` returns by value and the compiler doesn't apply NRVO, the compiler-generated move COPYES the pointer → temporary destructor frees the memory → destination pointer is DANGLING → later destructor frees again → double-free.

**Check**: Compile with `-fsanitize=address` (ASAN). The crash always happens during SAS verification — the SAS dialog opens and crashes immediately.

**Fix**: Add move constructor + move assignment that transfer ownership (set `other.sas = nullptr`). Delete copy constructor/assignment (SasSession owns heap memory).

**Status**: Fixed Aug 1. Additionally, `olm_sas_set_their_key` decodes the pubkey base64 IN-PLACE (libolm quirk #9) — pass a COPY (`a91ca63`).

**Verify**: Create two SasSessions, exchange pubkeys, compute emojis — must NOT crash. Run ASAN build — must be clean.

---

### SAS emojis intermittently 6 instead of 7 (miss Hammer) — Element mismatch

```
Our SAS dialog shows 6 emojis (random one missing). Element shows 7. m.mismatched_sas cancel.
```

**Root cause**: The 64-emoji table was missing its 64th entry (Hammer 🐶 / index 63). `index % 64` never yielded 63, so one emoji position silently collapsed → only 6 emojis displayed vs Element's 7 → mismatch cancel. Intermittent because index 63 only occurs for certain SAS byte values.

**Fix**: Add the missing 64th entry to the emoji table in `sas_emojis.cpp` (`e27d555`). Verify the table has exactly 64 entries, indexed 0-63.

**Verify**: Two-manager protocol test (`test_e2ee_verify_protocol.cpp`) → emoji match path passes. With Element → verify → both show 7 identical emojis.

---

### Two-manager SAS test fails on pubkey/MAC (olm_sas_set_their_key clobbers buffer)

```
Two-manager test: manager A's theirSasPubkey is garbage after setTheirKey → emojis/MAC mismatch.
```

**Root cause**: `olm_sas_set_their_key` decodes the base64 pubkey IN-PLACE (`b64_input` pattern, same as quirk #6). The caller passes its stored pubkey string directly → after the call the buffer is the raw decoded bytes, so the stored `theirSasPubkey` member is corrupted → every later use (emoji, MAC, commitment) produces wrong output.

**Fix**: Pass a copy: `std::string copy = theirSasPubkey; sas.setTheirKey(copy);` (`a91ca63`). This is the 9th documented libolm quirk.

**Verify**: Two-manager SAS protocol test passes. SAS crypto roundtrip test (`test_e2ee_sas.cpp`) passes.

---

### SAS emojis don't match Element (7 emojis expected, 2 shown)

```
Our SAS dialog shows 2 emojis. Element shows 7 emojis. m.mismatched_sas cancel.
```

**Root cause**: `computeSasDecimals` uses `for (i=0; i+2 < size; i+=3)` — non-overlapping 3-byte windows → 2 values from 6 bytes. Matrix spec requires 7 overlapping 13-bit windows from the same 6 bytes.

**Fix**: Rewrite as the spec overlapping-window algorithm:
```cpp
uint64_t num = 0;
for (int i = 0; i < 6; i++) num = (num << 8) | sasBytes[i];
for (int i = 0; i < 7; i++) {
    int shift = 48 - (i+1)*13; if (shift < 0) shift = 0;
    decimals[i] = ((num >> shift) & 0x1FFF) % 10000;
}
```

**Verify**: With Element on same account → Settings → "Verify this device" → both show 7 emojis → must MATCH.

---

### Element rejects SAS MAC (m.key_mismatch cancel)

```
Element shows emoji match confirmed, then cancels with m.key_mismatch.
Our log: "verification: state=10 (Cancelled)"
```

**Root cause**: MAC info string format wrong. Our code: `"MATRIX_KEY_VERIFICATION_MAC|<txn>|<dev>|<edkey>|<curvekey>"` — three errors: (a) pipe separators (spec uses concatenation, no separators), (b) both keys in ONE string (spec is per-key), (c) key VALUES not key IDs. Spec format per key: `"MATRIX_KEY_VERIFICATION_MAC" + txnId + deviceId + "ed25519:" + deviceId` (no separators, per-key, keyId format).

**Fix**: Change `macInfo` to take a single `keyId` parameter (e.g., `"ed25519:DEVICEID"`). Return concat without separators. Call separately for each key.

**Verify**: With Element → verify → both send MAC → both receive MAC → verification complete → "done" state for both.

---

### Registration token: m_bad_json "missing field token"

```
[NET] /register http=400
[LOG] Registration failed (M_BAD_JSON): deserialisation failed: missing field token at line 1 column 104
```

**Root cause**: Wrong JSON field name. Synapse expects `"token":"<value>"` inside the auth dict. Our code sends `"registration_token":"<value>"`. The auth TYPE is `m.login.registration_token` (correct), but the FIELD holding the value is just `"token"` (we wrote `"registration_token"`).

**Check**: The error message literally says "missing field **token**" — Synapse is telling you the field name it expects.

**Fix**: Change `"registration_token"` to `"token"` at `matrix_client.cpp registerAccount`. Also add the token field to the 401 retry body (was missing entirely).

**Verify**: Register on a token server with valid token → succeeds. Register without token → works as before.

---

### ColorSettingsDialog opens empty (no color rows)

```
User opens Settings → Colors → dialog is empty (only Apply/Cancel visible, no color swatches)
```

**Root cause**: `addRow()` calls `findChild<QFormLayout*>()` which searches `this->children()`. But `formContainer` is NOT a child of `this` until `scroll->setWidget(formContainer)` runs. At `addRow()` call time, `findChild` returns `nullptr` → `if (form)` guard skips → all 22 rows silently dropped.

**Fix**: Pass the `QFormLayout*` pointer directly to `addRow` as a parameter instead of using `findChild`. Must pass it BEFORE `scroll->setWidget(formContainer)`.

**Verify**: Open Colors dialog → all 22 color rows visible with swatches. Change "Accent" → UI updates immediately.

---

## Log grep cheat sheet

```bash
# Successful E2EE flow
grep -E "DECRYPTED|handleRoomKey OK|shared=1|loaded.*sessions|room key shared ok|deviceCount=[2-9]|claimed [2-9]"

# E2EE failures
grep -E "FAILED|not found in ciphertext|no megolm session|400|corrupted|SIGABRT|AAAAAAA|loaded.*0"

# OTK/count tracking
grep -E "OTKs sufficient|uploaded_key_count|signed_curve25519 count|OTK count updated"

# Device key mismatch
grep -E "Olm: our curve25519|Olm: ciphertext has key"

# Device reset
grep -E "deleteDevice|Reset device|device reset"

# Account switch
grep -E "before-account-switch|after-account-switch|loaded.*megolm|loaded.*olm"

# Upload flow
grep -E "uploadDeviceKeys:|shared=|needDeviceKeys=|marked as shared"

# SIGSEGV on close (detached sync thread)
grep -E "setUploadedKeyCount|worker_.detach|worker_.join|CRASH.*Signal 11"

# Olm session exponential growth
grep -E "loaded.*olm session pickles|cleanup.*loaded"
# If loaded count > 1000 → exponential growth bug. Check RAM: should be < 1000 after fix.

# SAS verification flow
grep -E "m.key.verification|verification.*state|computeSasEmojis|sasCreate|MAC|commitment"
# If "CANCELLED" appears → check MAC info string format or commitment hash.

# Registration token
grep -E "m_bad_json.*missing field token|m.login.registration_token"
# "token" is the field name, NOT "registration_token".

# Color system
grep -E "findChild.*QFormLayout|addRow|Design::|inline hex"
# 0 inline hexes should remain outside theme.cpp/hpp.
```
