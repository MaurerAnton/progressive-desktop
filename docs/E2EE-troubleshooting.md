# docs/E2EE-troubleshooting.md — E2EE Diagnostic Guide

> **Who this is for:** Developers debugging E2EE issues in Progressive Chat.
> Quick reference: grep the logs for the symptom → read the cause → apply the fix.
> **Last updated:** July 29, 2026

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
```
