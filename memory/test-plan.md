# memory/test-plan.md — Alpha E2EE Acceptance Test Plan

> **When to run**: After all P0-P3 E2EE fixes are pushed. Each phase builds on the previous.
> **Setup**: Create FRESH test accounts (not @t1s3/@t0n1a — those have 167 stale OTKs from the broken period).
> **Run with**: `./build/progressive-desktop 2>&1 | tee test-$(date +%Y%m%d-%H%M).log`

---

## Test Accounts

| Account | Client | Role |
|---|---|---|
| `@test_alpha_1:matrix.org` | Progressive (PineTab) | Main test — our client |
| `@test_alpha_2:matrix.org` | Element/Faraday (phone/desktop) | Sends TO Progressive — tests inbound |
| `@test_alpha_3:matrix.org` | FluffyChat (phone) | Cross-implementation test |
| `@test_alpha_1:matrix.org` on Faraday | Element (phone) | Multi-device — same user, different device |

---

## Phase 1: Clean Slate (fresh accounts, no stale OTKs)

### Setup
```
[ ] Pull + build on PineTab
[ ] Create @test_alpha_1 on Progressive (fresh account)
[ ] Create @test_alpha_2 on Faraday/Element (fresh account)
[ ] Delete old Progressive DB if you want 100% clean state:
    rm ~/.local/share/progressive-desktop/session.db
```

### Test
```
[ ] Faraday: New room → enable encryption → invite @test_alpha_1
[ ] Progressive: accept invite
[ ] Check log: "uploadDeviceKeys: shared=0 needDeviceKeys=1" (first upload)
[ ] Check log: "uploadDeviceKeys: SUCCESS"
[ ] Faraday: send "hello encrypted world"
[ ] Progressive: decrypts? → "[E2EE] DECRYPTED eid=..."
[ ] Check log: "Olm: our key found in ciphertext" (NOT "not found")
[ ] Check log: "handleRoomKey OK"
[ ] Progressive: send "hi back"
[ ] Faraday: decrypts?
[ ] Progressive: close → restart
[ ] Check log: "shared=1 needDeviceKeys=0" (skip upload)
[ ] Check log: "uploadDeviceKeys: OTKs sufficient (count=N), skipping"
[ ] Check log: "loaded N megolm sessions" (N > 0)
[ ] Old messages ("hello encrypted world") still decrypt?
[ ] Faraday: send "after restart test" → Progressive: decrypts?
```

### Pass criteria
- [ ] No 400 "already exists" errors
- [ ] No "Olm: our key not found in ciphertext"
- [ ] No "corrupted size" or SIGABRT
- [ ] "loaded N megolm sessions" where N > 0 after restart
- [ ] Old messages decrypt after restart
- [ ] New messages decrypt after restart
- [ ] Both directions decrypt (Faraday→Progressive AND Progressive→Faraday)

---

## Phase 2: Persistence (restart)

```
[ ] Close Progressive
[ ] Restart → check logs for megolm/Olm session counts
[ ] All old messages decrypt? (not [encrypted])
[ ] Faraday: send "persistence check" → Progressive: decrypts?
[ ] Progressive: send "persistence reply" → Faraday: decrypts?
[ ] Restart Progressive AGAIN → old messages still decrypt?
```

### Pass criteria
- [ ] Megolm sessions loaded after each restart (N > 0)
- [ ] Olm sessions loaded after each restart
- [ ] Old messages decrypt after 3+ restarts
- [ ] New messages sent after restart decrypt on other client

---

## Phase 3: Multi-Account Switch

```
[ ] Progressive: add @test_alpha_3 as second account (via "Add Account")
[ ] Switch to @test_alpha_3
[ ] Check log: "shared=0 needDeviceKeys=1" (new account upload)
[ ] Check log: megolm sessions loaded
[ ] Check log: curve25519 = real key (not "AAAA...")
[ ] @test_alpha_3 → send msg to Element → decrypts?
[ ] Element → send msg to @test_alpha_3 → decrypts?
[ ] Switch back to @test_alpha_1
[ ] Check log: megolm sessions loaded for @test_alpha_1
[ ] Old @test_alpha_1 messages decrypt?
[ ] Switch @test_alpha_1 → @test_alpha_3 → @test_alpha_1 (rapidly, 3x in 10 seconds)
[ ] No crash? No deadlock?
```

### Pass criteria
- [ ] No crash on account switch (no "corrupted size" or SIGABRT)
- [ ] curve25519 stable across switches (same key in logs)
- [ ] Each account loads its own megolm sessions (not 0)
- [ ] Each account has its own upload state (shared flag per account)
- [ ] Old messages decrypt after switching back

---

## Phase 4: Multi-Device (1 user, 2 devices)

```
[ ] Setup: @test_alpha_1 logged in on Progressive (PineTab)
[ ] Setup: @test_alpha_1 logged in on Faraday (Element, phone) — SAME user, different device
[ ] @test_alpha_2 creates encrypted room with @test_alpha_1
[ ] @test_alpha_2 sends "multi-device test"
[ ] Progressive: decrypts? → "[E2EE] DECRYPTED"
[ ] Faraday: decrypts? → plaintext visible
[ ] Check log: "shareRoomKey: user=@test_alpha_1 deviceCount=2"
[ ] Check log: "claimed 2 one-time keys" (should claim for BOTH devices)
[ ] Progressive: send "reply from pinetab"
[ ] Faraday: decrypts?
[ ] @test_alpha_2: decrypts?
```

### Pass criteria
- [ ] Both Progressive AND Faraday decrypt the same message
- [ ] "deviceCount=2" in shareRoomKey log
- [ ] Both devices receive room_key (grep "found device: @test_alpha_1")
- [ ] If only Faraday decrypts (Progressive shows [encrypted]):
    → Settings → "Reset device keys" → restart → re-test
    → Both should decrypt after device reset

---

## Phase 5: Device Reset (stale OTK recovery)

```
[ ] Progressive: Settings → "Reset device keys" → enter password
[ ] Check log: "deleteDevice: http=200 ok=1" (or similar success)
[ ] Restart Progressive
[ ] Check log: "shared=0 needDeviceKeys=1" (fresh upload after reset)
[ ] Check log: "uploadDeviceKeys: SUCCESS"
[ ] Check log: "uploadDeviceKeys: account marked as shared"
[ ] @test_alpha_2 sends message → Progressive decrypts?
[ ] Faraday (multi-device) sends message → Progressive decrypts?
```

### Pass criteria
- [ ] Device deleted on server (POST /delete_devices returns 200)
- [ ] Fresh upload after restart (shared=0)
- [ ] Both other clients can send encrypted messages to Progressive
- [ ] No stale OTKs remaining (OTK count starts fresh, not 167)

---

## Phase 6: FluffyChat Cross-Client

```
[ ] @test_alpha_3 (FluffyChat) creates encrypted room
[ ] Invites @test_alpha_1 (Progressive)
[ ] Progressive: accept invite
[ ] FluffyChat: send message → Progressive: decrypts?
[ ] Progressive: send reply → FluffyChat: decrypts?
[ ] Check log: no "Olm: our key not found in ciphertext"
[ ] Check log: "DECRYPTED" for FluffyChat messages
```

### Pass criteria
- [ ] Both directions decrypt (FluffyChat→Progressive AND Progressive→FluffyChat)
- [ ] No Olm key mismatch errors
- [ ] No "no megolm session" errors

---

## Phase 7: Edge Cases

```
[ ] Send message, close app DURING send → restart → no crash
[ ] Leave encrypted room → rejoin → messages decrypt?
[ ] Create encrypted room from Progressive → invite others → they decrypt?
[ ] Send image in encrypted room → check for crash (image preview is separate bug)
[ ] Account A sends → switch to B while sync running → no crash → switch back → A's messages decrypt?
[ ] OTK auto-refresh: observe logs for "OTKs sufficient" vs "uploading fresh keys"
[ ] Token refresh: observe logs for "M_UNKNOWN_TOKEN" → "/refresh" → recovery
```

### Pass criteria
- [ ] No crash on any edge case
- [ ] Messages decrypt after rejoin
- [ ] Room created from Progressive works (both directions)
- [ ] No state corruption during rapid account switches
```

---

## Phase 8: SAS Verification (device-to-device)
[Depends on: Phase 2 SAS state machine + dialog]

```
[ ] Progressive + Element (same account): Progressive → Settings → "Verify this device"
[ ] Element: accepts incoming verification request
[ ] Both: 7 emojis displayed — do they match?
[ ] Both: confirm match → verification complete
[ ] Element: Progressive device shows GREEN shield (verified)
[ ] Progressive: send message → Element: green shield on room
[ ] Progressive: disconnect → reconnect → device still verified?
[ ] FluffyChat: verify with Progressive → emojis match?
[ ] Cancel mid-flow → m.key.verification.cancel sent
[ ] Timeout: wait 10 min without confirming → verification expires
[ ] User verification: open user profile → "Verify"
```

### Pass criteria
- [ ] Emojis match on both sides (7 emojis, identical)
- [ ] Device shows green shield after verification
- [ ] Verification survives disconnect/reconnect
- [ ] Cross-client works (Progressive ↔ Element, ↔ FluffyChat)
- [ ] Cancel propagates correctly
- [ ] Timeout handles gracefully

---

## Phase 9: Cross-Signing (trust chain)
[Depends on: Phase 6 cross-signing implementation]

```
[ ] Settings → "Set up secure messaging" → generate MSK/USK/SSK
[ ] Enter recovery passphrase → bootstrap complete
[ ] Element (same account): cross-signing shows MSK published
[ ] Verify another device (your Element session)
[ ] Element: both devices show green shield
[ ] New device login: verify via existing device → trust propagated
[ ] Cross-signing reset: Settings → "Reset identity" → re-verify
```

### Pass criteria
- [ ] MSK/USK/SSK generated + published to account_data
- [ ] Trust propagates (verify one device → all devices verified)
- [ ] New device verifiable via existing device
- [ ] Cross-signing reset works

---

## Phase 10: Key Backup (SSSS + server-side)
[Depends on: Phase 7 SSSS + key backup]

```
[ ] Settings → "Set up key backup"
[ ] Option A: generate recovery key → display → save it
[ ] Option B: enter recovery passphrase → confirm
[ ] Backup version created (POST /room_keys/version → 200)
[ ] Send messages in several encrypted rooms
[ ] Check backup: GET /room_keys/version → count > 0
[ ] Logout (clear session.db) → login again (fresh session)
[ ] Restore from backup: enter recovery key → history restores
[ ] Old messages decrypt after restore?
[ ] Delete backup: DELETE /room_keys/version → 200
```

### Pass criteria
- [ ] Recovery key generated + displayed (base58, correct format)
- [ ] Backup version created + session keys uploaded
- [ ] History restores after fresh login
- [ ] Both recovery key AND passphrase paths work
- [ ] Backup deletion works

---

## Phase 11: Fallback Keys
[Depends on: Phase 3 fallback keys port]

```
[ ] Exhaust all 100 OTKs (let auto-refresh upload only when low, or claim rapidly)
[ ] Check: /sync returns device_unused_fallback_key_types: ["signed_curve25519"]
[ ] New device creates Olm session via fallback key → succeeds
[ ] Element: send encrypted message → Progressive decrypts
[ ] Fallback key used → mark as published → generate new fallback
```

### Pass criteria
- [ ] Fallback key generated + uploaded
- [ ] Server reports fallback key available
- [ ] Other clients can create Olm session via fallback key
- [ ] Fallback key rotated after use

---

## Phase 12: Key Sharing + Forwarded Keys
[Depends on: Phase 4 key sharing + forwarded_room_key]

```
[ ] New device (FluffyChat, same account): joins room
[ ] FluffyChat sends m.room_key_request → Progressive receives
[ ] Progressive: shouldShareKey → yes (verified) → send m.forwarded_room_key
[ ] FluffyChat: import forwarded key → decrypt history
[ ] Untrusted device request: shouldShareKey → no → don't forward
[ ] Request cancellation + expiry (10-min window)
```

### Pass criteria
- [ ] Incoming m.room_key_request received + handled
- [ ] shouldShareKey gates on trust
- [ ] m.forwarded_room_key sent + imported correctly
- [ ] Untrusted device request rejected

---

## Phase 13: Megolm Rotation + Forward Secrecy
[Depends on: Phase 5 Megolm rotation]

```
[ ] Set rotation_period_msgs = 5 (via Element state event)
[ ] Send 6 messages → session rotates after message 5
[ ] New session_id in logs
[ ] Late joiner: can decrypt new messages but NOT pre-rotation
[ ] Existing devices: can decrypt ALL messages
```

### Pass criteria
- [ ] Session rotates at message count + time thresholds
- [ ] Forward secrecy: late joiners cannot read pre-rotation history
- [ ] Existing devices decrypt through rotation boundary

---

## Phase 14: Security Validation
[Depends on: Phase 1 ed25519 verify + Olm plaintext validation]

```
[ ] Tampered OTK signature → verify fails → OTK rejected
[ ] Tampered device key signature → verify fails → device rejected
[ ] Olm plaintext: mismatched sender/recipient → REJECTING in logs
[ ] Ed25519 verify roundtrip: sign → verify OK, tamper → FAIL
[ ] Color centralization: 0 inline hexes outside theme.cpp/hpp
```

### Pass criteria
- [ ] Tampered signatures rejected (OTK + device key)
- [ ] Olm plaintext field mismatches logged + rejected
- [ ] Ed25519 verify works (roundtrip + tamper rejection)
- [ ] Color centralization complete

---

## Summary — All Phases Quick Checklist
```
[ ] Phase 1: Basic E2EE (send + receive, restart)
[ ] Phase 2: Persistence (3 restarts)
[ ] Phase 3: Multi-account (switch, switch back, rapid switch)
[ ] Phase 4: Multi-device (2 devices decrypt same message)
[ ] Phase 5: Device reset (clear stale OTKs)
[ ] Phase 6: FluffyChat cross-client
[ ] Phase 7: Edge cases (crash during send, rejoin, create room)
[ ] Phase 8: SAS verification (device-to-device emoji match)
[ ] Phase 9: Cross-signing (trust chain, device shields)
[ ] Phase 10: Key backup (SSSS + server-side — restore history)
[ ] Phase 11: Fallback keys (OTK exhaustion recovery)
[ ] Phase 12: Key sharing + forwarded keys (re-share on request)
[ ] Phase 13: Megolm rotation + forward secrecy
[ ] Phase 14: Security validation (signature verification, Olm validation)
```

## Pass/Fail Criteria

**ALPHA READY** if: Phases 1-5 all pass with zero failures.

**BETA READY** if: Phases 1-10 all pass with zero failures.

**FULL E2EE READY** if: Phases 1-14 all pass with zero failures.

**NEEDS FIX** if: Any phase shows:
- "Olm: our key not found in ciphertext" (stale device keys)
- 400 "already exists" errors (OTK duplicates)
- "loaded N megolm sessions" where N = 0 after restart (persistence broken)
- curve25519 = "AAAA..." (double-init crash)
- SIGABRT / "corrupted size" (crash)
- "no megolm session" for messages from other client (can't decrypt inbound)
- SAS emojis don't match between devices (SAS decimal/emoji bug)
- Element rejects MAC with m.key_mismatch (MAC info string format wrong)
- Recovery key fails to restore history (backup crypto bug)

**EXPECTED NOISE** (not failures):
- "requestRoomKey: sent for room=!... sid=... sender=@SELF" (self-request — normal for self-echo)
- "no device found for senderKey=/OVaWlV/..." with senderKey matching our own (self-request — normal)
- M_UNKNOWN_TOKEN → /refresh → retry OK (token refresh — normal lifecycle)
