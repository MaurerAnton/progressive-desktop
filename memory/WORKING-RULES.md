# WORKING-RULES.md — User Interaction Preferences

> How to work with this user. Not project code rules — see AGENTS.md for those.
> Not prompt-writing rules — see PROMPTER.md for those.
> Last updated: July 29, 2026

---

## Quick Reference

| # | Rule | Action |
|---|---|---|
| 1 | No opencode question forms | Ask in plain text, ONE at a time |
| 2 | Verify AI coder claims | grep/read actual source — they've been wrong before |
| 3 | Open questions BEFORE prompts | Resolve ambiguity first, never write prompts with assumptions |
| 4 | Write answers FOR the AI coder | Copy-paste ready response with verified claims |
| 5 | Maximal effort in research | Compare actual source via gh/curl, quote real code |
| 6 | Honest about errors | Acknowledge mistakes, consider regressions from recent commits |
| 7 | Simple explanations | Explain Matrix/E2EE concepts (OTK, Megolm, Olm, etc.) |
| 7a | Explain C++ basics | lambda, shared_ptr, QPointer, mutex, XOR, pickle, base64 |
| 8 | Grammar correction | Fix articles/spelling/verb forms; NOT slang, case, punctuation |
| 9 | AI coder commits/pushes | I do NOT commit — delegate to AI coder |
| 9a | Exception: docs/memory | I CAN commit docs/, memory/, AGENTS.md (verify: no Russian, no secrets) |
| 10 | Test accounts by ROLE | Progressive, Element/Faraday, FluffyChat, Element Web |
| 11 | User tests on PineTab 2 | ARM64, DanctNIX |
| 11a | 9-step log analysis | startup → upload → crashes → incoming → recovery → Olm → OTK → switch → outbound |
| 12 | Multi-device context | Check if other device (Faraday) can decrypt |
| 13 | Be efficient when tired | Skip detail, actionable only |

---

## 1. Don't use opencode question forms
The multi-question form has a bug — the user has trouble with more than 1 question.
Present questions as PLAIN TEXT with pros/cons, not via the question tool.
If a question is absolutely needed, ask ONE at a time.

## 2. AI coder claims must be verified against actual source
The AI coder has been wrong before (said "submodule has no fallback key support"
when it did, said "SQLite silently succeeds" when it returns an error).
ALWAYS grep/read the actual source to verify claims before accepting them.
"ai coder could lie, dont forget about this"

## 3. Open questions BEFORE writing prompts
Never write a fix prompt while open questions remain. Resolve all ambiguity
first, then write the prompt. Writing a prompt with assumptions leads to
wrong implementations.

## 4. Write answers FOR the AI coder
When the AI coder submits remarks/review, the user wants me to:
- Verify each claim against actual source (rule #2)
- Write a clear "Answer to AI coder" response they can copy-paste
- Include: which claims are correct, which are wrong, what to do next

## 5. Maximal effort in research
User explicitly asked for "maximal effort in research and comparing."
When comparing with other clients, research ACTUAL source code via gh/curl,
not just documentation. Quote actual code patterns from real code.

## 6. Honest about errors
When I make a mistake (recommended Nheko pattern for multi-account when
Nheko has no multi-account), acknowledge it clearly. User values honesty
over confidence. "are u sure, that it doesn't just bug caused by some
of commits" — always consider regression from recent commits.

## 7. Simple explanations preferred — basics are appreciated
The user is NOT a Matrix protocol expert. They're a developer debugging
their own codebase. Simple explanations of concepts (what is an OTK?
what is a curve25519 key? why does the server have 167 of them?) are
welcome. Don't assume knowledge of Matrix E2EE internals. Explain
things clearly. The user is learning this as they go.

## 7a. Explain C++ basics when showing code
The user does not know all C++ patterns used in the codebase.
When showing a code snippet that uses these, explain what they are:
- **Lambda** — `[capture](params) { body }` — an anonymous function that
  can "capture" variables from the surrounding scope. Used for ThreadPool
  callbacks and QMetaObject::invokeMethod. Why: lets you write the callback
  right where you need it instead of in a separate function.
- **shared_ptr** — `std::shared_ptr<MatrixClient>` — a pointer that keeps
  a reference count. When the count goes to zero, the object is deleted.
  Why: prevents use-after-free crashes when multiple handlers hold the same
  client. If one handler is destroyed, the client stays alive for others.
- **QPointer** — `QPointer<MainWindow>` — a Qt pointer that becomes null
  when the object is deleted. Checked with `.isNull()`. Why: prevents crashes
  when a callback runs after MainWindow was destroyed (app closed).
- **mutex / lock_guard** — `std::mutex mtx_` prevents two threads from
  accessing the same data at the same time. `std::lock_guard<std::mutex> lk(mtx_)`
  locks and auto-unlocks when the scope ends. Why: prevents data races.
- **XOR encryption** — used in megolm_store.cpp for pickle data. XOR with
  a key string (cycling through the key characters). Same operation encrypts
  AND decrypts (XOR is reversible). Why: simple obfuscation on disk so
  session keys aren't stored as plain text.
- **pickle** — serialized state of a cryptographic object (Olm account,
  Olm session, Megolm session). Libolm pickles contain the internal state
  (keys, ratchet positions). Stored in SQLite. Why: save E2EE state across
  app restarts.
- **base64** — encoding that converts binary data to text (A-Z, a-z, 0-9, +, /).
  All Matrix E2EE data (keys, ciphertext) is base64-encoded in JSON.
  Important: libolm functions ALREADY handle base64 internally — never
  double-encode/decode. See E2EE.md Quirks 1-5.
- **OTK (one-time key)** — a single-use encryption key uploaded to the server.
  Other clients claim ONE OTK to create an Olm session. After claiming, the
  server deletes it. You need to upload more when the count runs low.
  MAX_ONE_TIME_KEYS = 100 (libolm limit).
- **Megolm session** — encryption state for a GROUP chat. Created once,
  shared with all room members via Olm 1:1. All messages in the room use
  the same Megolm session until it's rotated.
- **Olm session** — encryption state between TWO devices (1:1). Used to
  securely deliver Megolm room keys. Created by claiming an OTK.
- **to-device event** — a Matrix event sent directly between devices
  (not visible in room timeline). Used for m.room_key, m.dummy,
  m.room_key_request — the E2EE plumbing.

## 8. Grammar correction
When writing answers, correct the user's English for:
- Missing articles (a, the)
- Misspelled words
- Wrong verb forms
Do NOT correct:
- Slang abbreviations (u, ur, gonna, wanna)
- Upper/lower case
- Punctuation style

Example: if the user writes "i think, element was buggy and just didnot
sent invite" → correct to "Element was buggy and just did not send the
invite" (add articles, fix spelling, don't touch slang).

## 9. Commit + push is the AI coder's job — NOT mine
I plan, the AI coder executes. I verify, the user tests. The pipeline:
  Me (plan mode) → AI coder (implement) → User (test on PineTab) → Me (analyze logs)
Never commit or push myself — always delegate to the AI coder session.

### 9a. Exception: Non-code files (docs, memory, AGENTS.md)
Untracked or modified files in `docs/`, `memory/`, or `AGENTS.md` at repo root
are documentation/config — NOT application code. I CAN commit and push these
directly without delegating to the AI coder. Before committing, verify:
- No Russian words (grep -i -P '[а-яё]')
- No secrets, keys, or tokens
- Content is documentation/planning, not implementation code
Code files (src/, tests/, scripts/) — still delegate to AI coder.

## 10. Test accounts are temporary — don't hardcode them
The user creates FRESH test accounts for each testing session. Don't
reference specific account names (@t1s3, @t0n1a, @offcrise) as permanent
fixtures. They change. The currently active accounts are:
  Progressive (PineTab) — the user's own client
  Element/Faraday (phone) — reference implementation for testing
  FluffyChat (phone) — cross-implementation testing
  Element Web (desktop) — easier device key inspection
Refer to them by ROLE, not by name.

## 11. User tests on PineTab 2
All testing happens on a PineTab 2 (ARM64, DanctNIX). Build command:
  git pull && cmake --preset pinetab2 && cmake --build build -j4 && ./build/progressive-desktop
Logs are captured via stderr redirect. User pastes logs — analyze them.
Use grep to find relevant lines, don't ask the user to read full logs.

### 11a. Log analysis workflow (do this every time logs are pasted)
Analyze in this exact order. This is the pattern I follow every time:
1. **Check startup E2EE state**: grep for `shared=`, `needDeviceKeys=`, `loaded.*sessions`.
   - `shared=1 needDeviceKeys=0` = correct. `shared=0 needDeviceKeys=1` = first upload. `loaded.*0` = persistence broken.
2. **Check upload result**: grep for `uploadDeviceKeys: SUCCESS\|FAILED`. Look for 400, 401.
   - `FAILED — error=Token is not active` = token race (normal on startup, auto-retry expected).
   - `FAILED — error=One time key.*already exists` = duplicate OTK IDs (regression).
3. **Check for crashes**: grep for `corrupted\|SIGABRT\|AAAAA`.
   - SIGABRT with MegolmStore in backtrace = recursive mutex deadlock.
   - `curve25519=AAAA...` = double-init (Olm account zeroed).
4. **Check incoming messages**: grep for `DECRYPTED` vs `no megolm session`.
   - `DECRYPTED` = inbound E2EE works. `no megolm session` = missing room_key.
   - If `no megolm session` → check next: was there a toDevice event with the room_key?
5. **Check recovery chain**: grep for `forceNewOlmSession\|requestRoomKey\|toDevice`.
   - `forceNewOlmSession: sent m.dummy` = we asked for new Olm session.
   - `requestRoomKey: sent` = we asked for room_key. Log shows `sender=` — is it our own user (self-request) or another user?
   - `processToDevice: N toDevice events` — any response? If 0, no one sent us the key back.
6. **Check Olm decryption**: grep for `Olm: our curve25519\|Olm: ciphertext has key\|Olm: our key not found`.
   - If "our key not found" → other client encrypts for a DIFFERENT curve25519 → stale device keys on server OR stale cache on other client.
   - Compare the two keys. If they're different → stale keys. If same → different bug.
7. **Check OTK state**: grep for `signed_curve25519 count\|OTKs sufficient\|400`.
   - High count (167) = stale OTKs from old Olm accounts.
   - "OTKs sufficient" = skip upload (correct).
   - "400" = duplicate OTK IDs.
8. **Check account switch**: grep for `before-account-switch\|after-account-switch\|curve25519=`.
   - curve25519 should be STABLE across switches. If it changes → Olm account recreated (double-init bug or pickle load failed).
9. **Check outbound send**: grep for `doSend\|shareRoomKey\|deviceCount\|claimed`.
   - `deviceCount=N claimed N` = all devices got OTKs. `claimed 1 (had 2)` = one device has no OTKs.
   - `sendToDevice ok shared=1` = room_key sent. `shared=0` = failed.

The grep commands for each step are in `docs/E2EE-troubleshooting.md` ("Log grep cheat sheet" section).

## 12. Multiple devices context
The user often has the SAME account on multiple devices:
  Progressive on PineTab + Element/Faraday on phone = same user, different devices
When analyzing "can't decrypt" issues, check if the OTHER device can
decrypt. If Faraday decrypts but Progressive doesn't → the issue is
Progressive-specific (stale OTKs, missing room_key delivery).

## 13. User can be tired — be efficient
If the user says they're tired, be concise. Skip the detailed analysis
and present only the actionable findings. They can ask for more detail
if needed. Long explanations when the user is tired waste their energy.

---

## What files should an AI coder read for each task?

This section tells an AI coder what to READ FIRST before making changes.
Read the listed files in order — they provide context, existing patterns,
and edge cases.

### E2EE — device key upload / OTK management
```
1. docs/E2EE.md                  ← architecture overview, shared flag, OTK count
2. AGENTS.md #7 section          ← multi-account E2EE patterns (shared flag, double-init guard, markOneTimeKeysPublished order)
3. src/core/crypto/olm_account.hpp  ← OlmAccountStore: create, load, save, generateOneTimeKeys, markOneTimeKeysPublished, shared_, uploadedKeyCount_
4. src/core/crypto/olm_account.cpp  ← implementation of above
5. src/core/sync_engine.cpp      ← uploadDeviceKeys(), auto-refresh, count update
6. src/core/session_store.cpp    ← saveOlmAccount/loadOlmAccount, e2ee_data KV table
```

### E2EE — megolm / olm session persistence
```
1. src/core/crypto/megolm_store.cpp  ← pickleAll, unpickleAll (XOR + hex), sessionCount (mtx_ lock!)
2. src/core/crypto/decryptor.cpp     ← pickleOlmSessions, unpickleOlmSessions
3. src/core/session_store.cpp        ← saveMegolmSessions, loadMegolmSessions, e2ee_data keys
4. src/ui/handlers/e2ee_init_handler.cpp  ← load megolm/olm on startup
5. src/ui/handlers/account_switcher.cpp   ← save old, load new on switch
```

### E2EE — message encryption / decryption
```
1. src/core/crypto/decryptor.cpp  ← encryptMessage, decryptMegolmEvent, shareRoomKey, handleRoomKey, handleOlmEncryptedToDevice
2. src/core/crypto/decryptor.hpp  ← struct declarations, method signatures
3. src/ui/chat/chat_view.cpp      ← doSend() — encrypted send path with room_id in plaintext
4. src/ui/handlers/thread_handler.cpp  ← sendThreadReply — separate encryption path (duplicated)
```

### Multi-account switching
```
1. src/ui/handlers/account_switcher.cpp  ← switchAccount(), addAccount() — init order, save/load
2. src/ui/handlers/e2ee_init_handler.cpp ← init() with pickle + shared + uploadedKeyCount
3. src/core/session_store.cpp            ← per-account tables (olm_account multi-row, e2ee_data scoped)
4. src/ui/main_window.cpp                ← setClient(), setSessionStore() propagation
```

### Sync engine
```
1. src/core/sync_engine.cpp      ← run(), processToDeviceEvents, uploadDeviceKeys, device key upload
2. src/core/fast_sync.cpp        ← /sync JSON parsing — toDevice, device_lists, OTK count
3. src/core/fast_sync.hpp        ← FastSyncResponse struct — what fields are available
4. src/ui/handlers/sync_response_handler.cpp  ← sync → UI bridge
```

### Room loading / timeline
```
1. src/ui/handlers/room_handler.cpp     ← onRoomClicked, onLoadMoreClicked
2. src/ui/room/room_store.cpp           ← prepareRoomSyncUpdate, appendTimelineForRoom
3. src/ui/room/room_data_loader.cpp     ← loadHistory, loadMembers, batchLoadRoomStates
4. src/ui/timeline/timeline_model.cpp   ← appendBack, appendBackBatch, appendFront, clear
```

### Thread handling
```
1. src/ui/handlers/thread_handler.cpp   ← openThreadView, sendThreadReply, replyInThread
2. src/ui/handlers/room_context_menu.cpp ← showTimelineContextMenu — thread view/root logic
3. src/ui/handlers/room_handler.cpp     ← closeThreadView, openThreadView signals
4. src/ui/timeline/timeline_model.hpp   ← DisplayedEvent — isThreadReply, threadRootId, threadReplyCount
```

### Device key management (non-E2EE code)
```
1. src/core/matrix_client.cpp    ← getThreadReplies, sendThreadReply (unencrypted path!), downloadMedia
2. src/core/http_client.cpp      ← httpGet, httpPost, httpPut — auth headers, SSL
3. src/core/json_utils.cpp       ← jsonEscape (file-local in matrix_client.cpp), jsonUnescape
4. src/core/thread_pool.cpp      ← ThreadPool::instance().enqueue() pattern
```

### Session storage (SQLite)
```
1. src/core/session_store.cpp    ← ALL tables: account, sync_state, olm_account, e2ee_data, hidden_rooms
2. src/core/session_store.hpp    ← method signatures
3. src/core/account_info.hpp     ← AccountInfo struct
```

### General architecture
```
1. memory/CODE_MAP.md            ← file index + "where to find X" reference
2. memory/REFERENCE.md           ← project overview, design decisions, signal contracts
3. AGENTS.md                     ← coding rules, E2EE quirks, setClient audit, build commands
4. docs/E2EE.md                  ← E2EE architecture, 8 libolm quirks, multi-account patterns
5. memory/REFACTOR.md            ← known code smells, duplication maps
```

---

## Common AI Coder Mistakes (preventive checklist)

Before an AI coder implements any change, verify they have read:
- [ ] `AGENTS.md` — especially #3 (setClient propagation), #4 (setClient audit), #6 (libolm quirks)
- [ ] `docs/E2EE.md` — especially Quirks 1-8 if touching E2EE code
- [ ] `memory/CODE_MAP.md` — the relevant section for their task
- [ ] The actual source files their change touches (not just the plan)

Common mistakes AI coders make:
1. Adding `base64Decode()` before any `olm_*` function (Quirks 1-5)
2. Re-calling `olm_*_session(memory)` on existing memory (Quirk 7)
3. Not copying the pickle buffer before `olm_unpickle_*` (Quirk 8)
4. Forgetting `setClient()` propagation to new handlers (AGENTS.md #3)
5. `#include` in header instead of forward declaration (AGENTS.md rule)
6. Double-init of OlmAccount (calling init() then init(pickle) — zeros the struct)
7. Calling `sessionCount()` inside `unpickleAll()` (recursive mutex deadlock)
8. Generating OTKs without `markOneTimeKeysPublished()` first (duplicate IDs → 400)
9. Forgetting to add new files to CMakeLists.txt
10. Not running `setClient audit` before commit
