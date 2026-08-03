# E2EE Full Implementation Roadmap

> 7 phases from alpha E2EE to full spec compliance.
> Each phase = 1-5 AI coder sessions. Total: ~4-6 weeks.
> Last updated: August 1, 2026

## Current state (after alpha hardening)
- Olm 1:1 sessions (create, persist, decrypt) ✓
- Megolm inbound + outbound (encrypt, decrypt, share room_key) ✓
- OTK management (upload, count, smart generation) ✓
- Device keys (upload, sign, device_lists tracking) ✓
- Recovery chain (m.dummy + m.room_key_request outgoing) ✓
- Multi-account E2EE (shared flag, per-account scoping) ✓
- Outbound Megolm persistence ✓
- Ed25519 signature verification ✓ (Phase 1)
- Olm plaintext validation ✓ (Phase 1)
- OTK + device key signature verification ✓ (Phase 1)
- SAS device verification (m.sas.v1) ✓ (Phase 2 — complete)
- Live-Synapse E2EE integration test ✓ (CI, cross-account round-trip)

## Phase 1 — Signature verification foundation ✅ COMPLETE
The linchpin. Without ed25519 verify, no signature can be checked.
- Ed25519 verify primitive: wrap `olm_ed25519_verify` (PUBLIC API, olm.h:516) in `ed25519Verify(key, message, signature) → bool`. Replaces submodule stub.
- Olm plaintext validation: verify `sender`, `recipient`, `recipient_keys`, `keys` fields per m.olm.v1 spec.
- OTK + device key signature verification: verify signed_curve25519 + device_keys signatures.
- Tests: sign+verify roundtrip, tampered sig rejected.

Unlocks: cross-signing (Phase 6), SAS MAC verify (Phase 2).

## Phase 2 — SAS verification (device verification) ✅ COMPLETE
The user-visible "make the red shield go away."
- Port sas_verification.cpp (OlmSAS wrapper — REAL in submodule, 212L) → `sas.cpp`/`sas.hpp` ✅
- Port verification_utils.cpp (64-emoji table, decimal computation, message builders — REAL, 1 bug fixed) → `sas_emojis.cpp` ✅
- Implement m.key.verification.* state machine (request→ready→start→accept→key→mac→done/cancel) → `verification.cpp` ✅
- To-device + in-room routing for 8 verification event types ✅
- Qt SAS dialog: 7 emoji display, "They match"/"They don't match" buttons, cancel/timeout → `sas_verification_dialog.cpp` ✅
- Self-verification (Settings → "Verify this device") ✅
- User verification (User profile → "Verify", RoomMembersDialog right-click) ✅
- Two-manager protocol test (`test_e2ee_verify_protocol.cpp`) — full flow + corrupted-MAC cancel ✅

Status: 40+ commits Aug 1, 17 spec-compliance bugs fixed (base64 in-place, MAC info format,
commitment, MAC-before-Done, role inversion, HKDF protocol, emoji table 64th entry, cancel codes).
Two-manager protocol test green. Cross-client verification (Element/FluffyChat) remains to validate.

## Phase 3 — Fallback keys ✅ COMPLETE (Aug 2026)
Unblocks P4. Removes "OTKs exhausted → can't receive" failure mode.
- [x] Port generateFallbackKey + unpublishedFallbackKey + forgetOldFallbackKey from libolm to OlmAccount class
- [x] Wire device_unused_fallback_key_types from /sync → fallback generation trigger
- [x] Upload fallback_key on /keys/upload (via includeFallbackKey flag + uploadFallbackKey())
- [x] Mark fallback as published after successful claim (markOneTimeKeysPublished covers both)
- [x] Per-account cooldown + 5-min forget-old-fallback timer
- [x] CSPRNG fix: submodule generateRandomBytes replaced rand() with /dev/urandom
- [x] OIDC PKCE/state CSPRNG (submodule, replaces srand/rand)
- NOTE: accounts created BEFORE the CSPRNG fix have rand()-derived keys — reset device keys / re-login to regenerate
- [x] Tests: lifecycle, pickle roundtrip, /sync parse, RNG regression, claimed-fallback session

## Phase 4 — Key sharing + forwarded keys + export/import ✅ COMPLETE (Aug 2026)
- [x] Incoming m.room_key_request — policy toggle (verified-only vs all) via
      SAS-verified device persistence (session_store verified_devices table)
- [x] m.forwarded_room_key with MSC3061 shared_history, dedup by request_id
- [x] m.forwarded_room_key receive + import (olm_import_inbound_group_session,
      real session id via submodule addImportedSession)
- [x] Key export/import — MegolmSessionData envelope (submodule exportAllSessionsJson
      + outbound merge), PrefsDialog Export/Import buttons
- [x] Tests: export/import roundtrip

## Phase 5 — Megolm rotation ✅ COMPLETE (Aug 2026)
- [x] isRotationDue + parseEncryptionConfig ported (submodule, called directly)
- [x] Rotation wired: message-count + time-period triggers drop + recreate
      outbound (roomKeysShared reset -> new key re-shared)
- [x] Forward secrecy automatic: shareRoomKey only shares the current outbound
- [x] m.room.encryption state event parsed per room (sync loop)
- [x] Rotation test (rotation_period_msgs=1)

## Phase 4/5 hardening — Multi-device room-key delivery ✅ (Aug 3)
The multi-account/multi-device CI scenario (`test_multiaccount_multidevice`) caught TWO
real bugs in `shareRoomKey`'s JSON request bodies — JSON objects can't repeat a key, so
per-device loops produced `"@user":{dev1},"@user":{dev2}` and the server kept only the
last, silently dropping one device:
- [/keys/claim body] grouped per user (each device's OTK claimed)
- [/sendToDevice body] grouped per user (every device gets its own m.room_key)
- keys/query body deduped (benign before, now correct)
Scenario coverage: 3 members, alice on 2 devices (login helper), both alice devices
decrypt the sender's messages, device2 sends + device1 decrypts, late joiner invited +
decrypts, cross-signing publishing verified across devices.

## Phase 6 — Cross-signing (3-4 sessions) — 🔄 IN PROGRESS (Aug 2)
Depends on Phase 1 (ed25519 verify).
- ✅ MSK/USK/SSK ed25519 keygen + sign/verify (libsodium, `0806d83`)
- ✅ Setup + device signing: SSK signs device_keys, PrefsDialog "Set up secure messaging" (`db6c5f0`, `05827d2`)
- ✅ Spec-correct publishing: POST /keys/device_signing/upload (CrossSigningKey format) + UIA password flow (`1075b57`); full canonical key signature (`db5da04`)
- 🔄 In flight: AI coder (sync_engine.cpp/hpp modified, not committed to docs yet)
- [ ] Trust computation with REAL signature verification (Phase 1 primitive)
- [ ] UI: device shields (red/grey/green), cross-signing reset

## Phase 7 — SSSS + key backup (4-5 sessions)
The hardest phase — crypto core must be written from scratch.
- Port key_backup.cpp recovery key format (REAL — base58, parity, curve-key)
- Implement Megolm backup encryption: Curve25519 ECDH + AES-256-CBC + HMAC-SHA-256
- Backup version create/query/delete (POST/GET/DELETE /room_keys/*)
- Upload + download + decrypt session keys
- SSSS secret storage: store/read MSK private key encrypted
- UI: key backup setup (recovery key display, passphrase entry), backup restore on login

## Submodule FAKE-boilerplate trap warning
DO NOT PORT these files — they are auto-generated JSON-echo no-ops:
- All *_v4.cpp files (~220KB total): verification_v4, cross_sign_v4, key_backup_v4, secret_store_v4, etc.
- crypto_ops.cpp (34KB)
- sas_manager.cpp, backup_controller.cpp, gossip_manager.cpp
- Most *_utils.cpp, *_manager.cpp files (with exceptions noted above)
- dehydrate_utils.cpp, key_share.cpp (the fake one), key_share_handler.cpp

## REAL submodule files safe to port
- sas_verification.cpp (212L) — OlmSAS wrapper (13 olm_sas_* calls)
- verification_utils.cpp (156L) — 64-emoji table + builders (1 known bug)
- keyshare.cpp (103L) — incoming key request policy + builders
- room_encryption.cpp (123L) — rotation logic
- key_backup.cpp (329L) — recovery key format only (not backup crypto)
- cross_signing_manager.cpp (342L) — data model only (not crypto)
- crypto_algorithms.cpp — SHA/HMAC/HKDF primitives
- olm_session.cpp:157 — fallback key (on OlmAccountData API)
- megolm_decryptor.cpp — inbound Megolm export

## What's missing (must write from scratch)
- Ed25519 verify primitive → ✅ DONE (Phase 1)
- Full SAS state machine → ✅ DONE (Phase 2)
- Cross-signing keygen + signing → Phase 6
- SSSS key backup encryption → Phase 7 (hardest — no submodule code)
- QR verification → not planned
- Device dehydration → not planned

## Live-Synapse CI regression guard (done)
`.github/workflows/synapse-e2ee.yml` runs a real Synapse container and executes
`tests/test_synapse_e2ee.cpp`: register 2 users → init decryptors → upload device keys →
create encrypted room (invite+join) → share room key → send encrypted event → bob decrypts.
Green on every push to main. Same test skips locally when no server (local `ctest` stays 100%).

## Spec references
- Matrix spec: "End-to-End Encryption", "Key management", "Server-side key backups", "Secret Storage", "Cross-signing", "Key verification", "To-device messaging"
- MSC1756 (cross-signing), MSC1543 (QR verification), MSC3061 (shared history)
- MSC3976 (fallback key usage), MSC3847 (device dehydration)
- MSC3894 (secret gossiping), MSC3086 (SAS emoji set)

---

# Diagnostic Guide (was docs/E2EE-troubleshooting.md, merged Aug 1)

> Quick reference: grep the logs for the symptom → read the cause → apply the fix.

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

**Causes** (in order of likelihood):
1. **167 stale OTKs** — orphaned OTKs from old Olm accounts on the server. /keys/claim returns a stale OTK → Olm session creation fails → other client gives up.
2. **Stale device key cache** — other client cached our old curve25519 from a previous /keys/query. Our new /keys/upload didn't trigger `device_lists:changed` in their /sync.
3. **OTK upload failed** (400 "already exists") — duplicate OTK IDs → no fresh OTKs on server → other client can't create Olm session.

**Fix**: "Reset device keys" in Settings → deletes device from server (clears ALL stale OTKs) → re-uploads fresh device_keys + OTKs. Then restart other clients (or wait for `device_lists:changed`).

---

### 400 "One time key already exists"

```
[E2EE] uploadDeviceKeys: FAILED — error=One time key signed_curve25519:AAAACg already exists
```

**Cause sequence**: libolm generates sequential OTK IDs; after restart it reuses the sequence → duplicate → server rejects. Generate → upload → `markOneTimeKeysPublished()` NOT called (e.g., 401 token race) → old OTKs accumulate → 400 on each restart → eventually hits MAX_ONE_TIME_KEYS=100.

**Fix**: Call `markOneTimeKeysPublished()` BEFORE generating new OTKs. Use OTK count from /sync to only generate `max(0, 100 - serverCount)` new OTKs. Skip upload when count is sufficient and `shared=true`.

---

### "corrupted size vs. prev_size while consolidating" CRASH

```
[CRASH] Signal 6 (SIGABRT)
[BACKTRACE] #7 MegolmStore::sessionCount()
            #8 MegolmStore::unpickleAll()
```

**Root cause**: Recursive mutex lock. `unpickleAll()` holds `mtx_` (line 108), then calls `sessionCount()` which tries to lock `mtx_` again (line 164) → deadlock → SIGABRT.

**Fix**: Replace `sessionCount()` with `impl_->mgr.sessionCount()` — direct access, no lock needed (already holding it).

---

### curve25519 = "AAAA..." (all zeros)

```
[E2EE] uploadDeviceKeys: our curve25519=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
```

**Root cause**: libolm quirk #7 — `create()` zeros the struct when called on an already-created account. Double-init in `switchAccount()`: `init()` then `init(pickle, key)` → `init(pickle)` fails to load → falls back to `create()` → zeros.

**Fix**: Remove the `init()` pre-check. Call `init(pickle, key, shared)` directly — it handles both load-from-pickle and create-new cases.

---

### "no device found for senderKey" during forceNewOlmSession

If senderKey matches `our curve25519=` → self-request (our other device sent the message). Normal, not a bug. If different key → our device_keys not on server (stale `otk_uploaded_once` flag).

---

### Account switch: megolm sessions lost (loaded 0)

`e2ee_data['megolm']` single key → account switch overwrites data. **Fix**: per-account scoping `e2ee_data['megolm:userId/devId']`.

---

### SIGSEGV on app close (Signal 11)

Detached sync worker thread → use-after-free. `worker_.detach()` → thread keeps running after `~SyncEngine()` destroys `decryptor_` → thread writes to freed memory.

**Fix**: `worker_.join()` instead of `detach()` in `sync_engine.cpp stop()`. Add `if (!running_) break;` defense-in-depth before decryptor_ accesses.

---

### App freezes on startup — "loaded session" repeating forever

```
[e2ee] olm: loaded session CpZiYQSb1EfmyJU9yvW5CiI9N69jPs (pickleLen=352) ... (30000+ repeats)
```

**Root cause**: Exponential Olm session growth — `switchAccount` appends sessions without clearing. Each switch compounds → 30000+ sessions → minutes to load, 250 MB RAM.

**Fix**: (1) `olmSessions_.clear()` at start of `unpickleOlmSessions`. (2) Dedup before push_back. (3) Cap 20/senderKey. (4) Same for `unpickleOutboundSessions`. (5) Auto-cleanup if loaded > 500 KB.

**Verify**: `loaded.*olm session pickles` ~30-60, not 30000. RAM < 150 MB. Startup < 5s. Switch 5x → stable.

---

### Ctrl+Tab logs out both accounts

Ctrl+Tab cycles through ALL combo indices (incl. separator, "+Add Account", "Logout"). Index 4 → `logout()` → `clearAccount()` (DELETE with NO WHERE) → both accounts nuked.

**Fix**: Ctrl+Tab only cycles `0..accountCount-1`. `clearAccount()` uses `DELETE FROM account WHERE user_id=?`.

---

### SasSession double-free CRASH (Phase 2 SAS)

`SasSession` owns `void* sas` + frees in destructor, but no move ctor/assignment → compiler-generated copy double-frees. **Fix**: move ctor/assignment transferring ownership, delete copy. (Fixed Aug 1.)

---

### SAS emojis intermittently 6 instead of 7 (missing Hammer)

The 64-emoji table was missing its 64th entry (index 63). `index % 64` never hit 63 → one position collapsed → 6 vs 7 → mismatch cancel. **Fix**: add the 64th entry (`e27d555`).

---

### Two-manager SAS test fails on pubkey/MAC (olm_sas_set_their_key clobbers buffer)

`olm_sas_set_their_key` decodes the base64 pubkey IN-PLACE (quirk #9). **Fix**: pass a copy: `std::string copy = theirSasPubkey; sas.setTheirKey(copy);` (`a91ca63`).

---

### SAS emojis don't match Element (2 shown, 7 expected)

`computeSasDecimals` used non-overlapping 3-byte windows → 2 values. Spec requires 7 overlapping 13-bit windows. **Fix**: rewrite with the overlapping-window algorithm (shift 48-(i+1)*13, & 0x1FFF, % 10000).

---

### Element rejects SAS MAC (m.key_mismatch cancel)

MAC info string format wrong: pipe separators (spec uses concatenation), both keys in one string (spec is per-key), key VALUES not key IDs. **Fix**: `macInfo` takes single `keyId` (e.g., `"ed25519:DEVICEID"`), concat without separators, called per key.

---

### Registration token: m_bad_json "missing field token"

Synapse expects `"token":"<value>"` inside the auth dict, not `"registration_token"`. **Fix**: change field name at `matrix_client.cpp registerAccount`; also add token to the 401 retry body.

---

### ColorSettingsDialog opens empty (no color rows)

`addRow()` uses `findChild<QFormLayout*>()` which returns nullptr until `scroll->setWidget(formContainer)` runs → all 22 rows silently dropped. **Fix**: pass the `QFormLayout*` pointer to `addRow` as a parameter, before `setWidget`.

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
# If loaded count > 1000 → exponential growth bug. RAM should be < 1000 after fix.

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
