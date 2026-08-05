# memory/LESSONS.md — What I've been taught (and quizzes)

> Plain-language lessons about how Progressive Chat works. Written for the user
> (not a coder) to reread and be quizzed on. If asked, I ask questions from here.

---

## Lesson 1 — The sync loop ("anything new?" every 3 seconds)

**Core idea:** Matrix works by ASKING. The server can't reach your app (your phone is
behind NAT/firewall), so the app opens a connection and waits on it.

- `SyncEngine::run()` runs on its own worker thread (`sync_engine.cpp:32`).
- It calls `client_->syncFast(since, timeout, false)` — the server holds the connection
  open (long-polling) and answers when there's news.
- Default poll timeout **3000 ms** (`sync_engine.hpp:129`) → the app keeps the line open,
  roughly asking every 3 seconds.
- **First sync** = 15s timeout, asks for current state of ALL rooms (the "Starting sync..."
  at login). Later syncs send a `since` token → "only what changed since last time."
- `M_UNKNOWN_TOKEN` (bad token) → retry 3×, then re-auth.

**Why not push?** Because nothing on the internet can open a connection INTO your device.
The server can only answer a connection the app already made. (Tor makes it worse — no
inbound at all — which is why the always-on presence-node daemon idea exists.)

**Key words:** long-polling, since token, incremental sync.

---

## Lesson 2 — Where your data lives

**Core idea:** One SQLite file on disk holds your app state. The server stores what's
*shared*; the file stores what's *yours*.

- File: `~/.local/share/progressive-desktop/session.db` (`main.cpp:236-242`).
- Tables (`session_store.cpp:50-78`):
  | Table | Holds |
  |---|---|
  | `account` | login (user ID, homeserver, token) |
  | `sync_state` | the "since" token (where you are in history) |
  | `olm_account` | your encryption identity (key pair) |
  | `e2ee_data` | encrypted room keys + session keys |
  | `hidden_rooms` | rooms you hid |
  | `cross_signing` / `verified_devices` | trust data |
- Your *messages* mostly live on the **server**; the file holds your **identity, keys,
  position**. Delete the file → lose encryption identity (can't read encrypted history) →
  that's why SSSS key backup (Phase 7) exists.

**Key words:** session.db, server-authoritative, local identity/keys.

---

## Lesson 3 — Why 160 MB RAM

**Core idea:** RAM = working memory. Most of the 160 MB is the Qt GUI framework, not
your code.

| Component | MB | Can reduce? |
|---|---|---|
| Qt framework | 40-60 | Hard (it's the GUI) |
| Timeline delegate | 20-30 | Yes (already virtualized) |
| simdjson buffers | 10-20 | Yes (~-5, reuse buffer) |
| Avatar cache | 10-20 | Yes (LRU cap) |
| SQLite | 5-10 | No |
| libcurl | 5-10 | No |
| Models + handlers | 10-20 | Some |

- The 60-80 MB goal comes from shaving the ~60 MB of "other" (delegate, caches, buffers) —
  small additive wins. Qt's ~50 MB is not strippable without removing the GUI.
- That's why "feature modules / GCC-style build selection" was deferred: the expensive
  stuff (Qt) is the stuff you can't remove.

**Key words:** Qt framework, virtual list, LRU cache, buffer reuse.

---

## Lesson 4 — Why the app doesn't freeze

**Core idea:** One "brain" (UI thread) draws the screen. Heavy work is moved to workers so
the brain stays free.

- **ThreadPool** (`thread_pool.hpp`): **4 worker threads** (`pool{4}`). Slow tasks
  (download, parse, encrypt) go here.
- **SyncEngine** has its own dedicated thread (`sync_engine.cpp:32`) — sync never blocks UI.
- Workers post results back to the UI thread via
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` — "do the slow thing on a worker,
  jump back to the UI when done."
- Mental model: UI thread = cashier; 4 workers = back room. Press Send → cashier hands the
  task to the back room and keeps serving.
- Cost: coordinating workers (locks, callbacks) is where subtle bugs live.

**Key words:** UI thread, worker thread, ThreadPool, QueuedConnection.

---

## Lesson 5 — Two loops, one memory: the interlock rules

**Core idea:** The app runs TWO loops at once (the sync worker + the UI). They share the
same memory. All the "scary" rules (`join`, `shared_ptr`, `setClient`, `atomic`,
`invokeMethod`) are just **safety interlocks** — like two machines sharing one conveyor:
only one may touch a shared part at a time, or they fight.

- **What a crash looked like (SIGSEGV on close):** close the app → window gone (UI loop
  dead) but the sync worker was still mid-scan. Old code used `detach()` ("don't keep
  track of the worker") → worker touched freed memory → crash.
- **The fix — `join()`** (`sync_engine.cpp:84`): `stop()` sets `running_ = false`, then
  `worker_.join()` WAITS until the worker actually finishes its current round and exits.
  "Don't close the door until the worker has walked out." `detach()` on a long-lived
  worker is the wrong tool; Nheko uses `detach()` only for a short-lived localhost server
  that dies on its own (SSOHandler.cpp:38) — that's the safe use.
- **Same fix in other clients:** Nheko's network engine (coeurl) does the identical
  `join()` on close (`lib/client.cpp:271-275`), plus a `detach()` guard for the corner
  case "close called FROM the worker." Element Web is JavaScript = one thread, no worker
  to join. Element X (Rust) = runtime owns threads and shuts them down itself.
- **Half-written values:** memory values are BIG (many fields), not one bit. If the UI
  reads while the worker writes, it can get cards 1-11 old + 12-20 new = a mix that never
  existed → wrong value/crash. `std::atomic<shared_ptr<AccountInfo>>`
  (`matrix_client.hpp:417`) fixes it by making the POINTER handover one indivisible step:
  you see the whole old stack or the whole new stack, never a mix. `shared_ptr` keeps the
  old stack alive while the UI finishes reading it.
- **What the model is really called:** your ThreadPool (`thread_pool.cpp:6-22`) is a
  master-slave model — standing workers grab jobs posted by `enqueue()`, and `~ThreadPool`
  `join()`s each worker (lines 30-32). Async runtimes (tokio) are the same idea with
  "smarter slaves": when a task waits on network, the worker swaps in another task instead
  of idling. Good for thousands of concurrent waits; not needed at our scale.

**Decision (recorded in REFERENCE.md):** keep ThreadPool. Adopt an async runtime ONLY if
the daemon ever reaches hundreds of concurrent connections — and even then, prefer
curl-multi/libevent (Nheko's coeurl) over a full async runtime.

**Key words:** join, detach, atomic, half-written, master-slave, async runtime.

---

## Quizzes

> Ask me any of these when I request a quiz. I answer from the lessons, and you can ask
> for verification against the code.

### Quiz 1 — Sync loop
1. Why can't the server just "push" messages to the app?
2. What does the app send on the FIRST sync vs later syncs?
3. Roughly how often does the app check for new messages?
4. What happens on an `M_UNKNOWN_TOKEN`?

### Quiz 2 — Data
1. Where is your app's data file, and what's it called?
2. What's stored locally vs on the server?
3. Why would deleting session.db make you lose access to encrypted history?

### Quiz 3 — RAM
1. What's the single biggest RAM consumer, and can it be removed?
2. Name two things that CAN be shrunk, and how.

### Quiz 4 — Threads
1. How many workers does the app have?
2. What's the "cashier / back room" analogy?
3. What pattern do workers use to update the UI?

### Quiz 5 — Thread safety (Lesson 5)
1. What does `join()` do, and what crash does it prevent?
2. What is a "half-written" value, and how does `std::atomic` fix it?
3. Are "smart slaves" (async runtimes) better than a ThreadPool? Should we switch?
4. What's the difference between your ThreadPool and tokio's model?
5. Why does `stop()` call `join()` and not `detach()` for the sync thread?

---

## Bet Rounds (multiple-choice game — what we learned)

> Format: I give a real scenario + 3 choices, user guesses, I reveal the code. Each round
> teaches one real mechanism. Score is tracked in conversation, not here.

### Round 1 — "You press Send" (correct: B — optimistic UI)
- The message appears INSTANTLY as a local **echo** (`chat_view.cpp:39-46`, `appendBack`),
  before the server answers. The echo is later replaced by the real event (`replaceEcho`)
  or by `❌ <error>` on failure (`chat_view.cpp:130,195`).
- Called **optimistic UI**: show-something-now feels better than waiting.
- Why "message appears twice" bugs existed: echo temp-ID vs server event-ID must match.

### Round 2 — "The 3-second question" (close, but the subtlety flips it)
- Design: `/sync?timeout=3000` = **long-poll**, not polling. The server holds the open
  connection and answers THE INSTANT new data arrives; 3s is only the "nothing new" ceiling.
- Polling = messages wait for YOU. Long-polling = you wait for messages.
- BUT real-world: Progressive→Progressive (plaintext) both wait ~3s; Element→Element instant;
  Progressive→Element arrives late on Element too → delay is on Progressive's SEND side.
  Tracked as a bug in PROGRESS.md (needs diagnostic LOGs, no guess-fix).

### Round 3 — "You click a room" (half right: frame cached, history not)
- Every room click CLEARS the timeline (`room_handler.cpp:88`) and re-fetches the last N
  messages over the network (`getMessages`, default 50). History is NOT cached.
- What IS cached and reused: room metadata (`stateLoaded` flag, `room_data_loader.cpp:301`),
  member avatars/names (`memberAvatarCache_`), avatar images (ImageLoader QCache).
- That's why the 2nd click is faster: the *frame* is cached, the *content* re-downloads.
- Element caches the timeline too → that gap = "Performance Mode P2" in DREAM.md.

### Round 4 — "Your internet drops 30s" (wrong: B IS implemented)
- Exponential backoff IS real: `computeBackoffMs` (`sync_engine.cpp:62-66`) =
  1s → 2s → 4s → 8s → 16s → 32s → 60s, applied via `cv_.wait_for` (`:202-208`).
- Success resets errors → backoff back to 1s. No manual reconnect needed.
- Lesson: resilience features are invisible until things break — "works when connected"
  doesn't mean "not implemented."

### Round 5 — "You log out" (correct: B — keys are kept)
- Logout calls `clearAccount` which runs ONLY `DELETE FROM account WHERE user_id=?`
  (`session_store.cpp:178`). The login row is removed; crypto keys in `olm_account`,
  `e2ee_data` (scoped `megolm:userId/devId`), `cross_signing` are NOT touched.
- NUANCE: re-login reuses device_id only if still set (`login_dialog.cpp:235-238`); if it
  generates a new UUID, old keys are orphaned → old encrypted history unreadable.
- That's exactly why SSSS key backup (Phase 7) exists.

### Round 6 — "Someone is typing" (correct: B — ephemeral, via sync)
- Typing arrives in the `/sync` response's **ephemeral** section (`fast_sync.cpp:114-130`,
  `m.typing`), is NOT saved to the database (`room_store.cpp:484` treats m.typing/m.receipt/
  m.fully_read as ephemeral), and only affects what's on screen now.
- Room list paints "X is typing..." (`room_list_delegate.cpp:200-210`).
- FRAGILE because it depends on sync timing (the ~3s issue from Round 2), isn't stored, and
  Progressive can't SEND typing yet (M2 — PUT /typing not built).
- **Verified gap (tracked as bug):** `RoomListModel::upsertRoom` (room_list_model.cpp:60)
  never copies `typingUsers` into the existing room and never emits dataChanged for it → the
  sidebar indicator likely doesn't refresh live. And there's NO in-chat typing UI at all.

### Round 7 — "3 rooms get messages while you're away" (none of A/B/C — the `break` quirk)
- Notification loop `sync_response_handler.cpp:78-88`: per sync cycle, exactly ONE popup fires —
  the FIRST unread room, then `break` (`:87`). NOT one-per-room in a single batch.
- User observed 2 rooms = 2 successive sync cycles each notified a different room (order/counts shift).
- `@mention in RoomName` body text IS implemented (`:83-84`).
- No dedup: a room that stays unread-and-first re-notifies every sync.
- Tracked as bug in PROGRESS.md.

### Round 8 — "Close + reopen 2h later" (correct: A, with the empty-since catch)
- A saved `since` token is loaded at startup (`sync_engine.cpp:28-30`) → incremental after first sync.
- BUT the FIRST sync deliberately uses empty `since` (`firstRun_=true`, `:32,88-92`) → "current
  state of all rooms" snapshot ("Starting sync..."). Saved token ignored on that call, overwritten
  with the new `next_batch` (`:216`). Not full history re-download (rules out B), not offline (rules out C).

### Round 9 — "Busy room, 200-message cap" (correct: B — sliding window)
- `MAX_TIMELINE_EVENTS = 200` (`timeline_model.cpp:75`); when exceeded, oldest events are dropped
  from the TOP (`:134-142`), IDs erased from seenIds_. New messages never blocked (rules out A);
  nothing saved to disk on eviction (rules out C — old msgs just leave memory).
- This is the RAM safety valve; scroll-up past the window needs a `/messages` re-fetch. That's why
  W16 (offline message cache) exists — persist last N events to SQLite so re-showing is local.

### Round 10 — "You click a file someone sent" (refused to guess — CORRECT reasoning)
- Intended path (code): click → `downloadMedia` → write `/tmp/progressive_*.pdf` →
  `QDesktopServices::openUrl` (`attachment_handler.cpp:42-62`). Temp file never cleaned/saved.
- BUT user's reality: card renders (color bar + 📄/🎵 + filename) yet CANNOT download/open.
- **CONFIRMED ROOT CAUSE:** `mxcUrl` only parsed for m.image/m.video (`room_store.cpp:376`),
  NOT m.file/m.audio → card paints (painter ignores mxcUrl, `timeline_painter.cpp:303`) but click
  silently no-ops (`timeline_delegate.cpp:217` requires !mxcUrl.isEmpty()). History path
  (`room_data_loader.cpp:110-122`) never sets mxcUrl at all.
- Lesson: "a file path exists, but the feature is broken in practice" — why we track bugs.
- Tracked in PROGRESS.md (Critical). Fix deferred by user request.

---

# Part 2 — written after the first 10 rounds (the E2EE + testing era)

> The first 10 rounds taught the *shape* of the app (sync, data, memory, threads).
> These lessons teach the part the app grew after that: encryption, trust, and how bugs get caught.

## Lesson 6 — What encryption actually does (the padlock story)

**Core idea:** A public key is not a hash or a fingerprint — it is a ONE-WAY LOCK.
The public key can only LOCK the box; the private key can only UNLOCK it; and
mathematically you cannot figure out the unlock from the lock. The private key
NEVER leaves your device — not even the server ever sees it.

- Two mechanisms, don't mix them up:
  - **Olm** = the *envelope* (delivers the key). One per PAIR of devices. Ephemeral.
  - **Megolm** = the *room key* (encrypts messages). One per SENDER device,
    shared with the whole room. All messages in a room from one sender use it.
- In a 30-device room you need ~1 key per device that actually SENDS — not 30×29.
  That collapse is the entire point of Megolm. This is why rooms stay fast.
- The server = **phone book** (stores everyone's Public padlocks, `/keys/query`)
  + **courier** (delivers locked envelopes by device ID). It knows the ADDRESS
  (device ID) but can never open the box.
- First message in an encrypted room is a beat slow: the room key makes a separate
  delivery trip first (`shareRoomKey` → `/sendToDevice` → `olm_encrypt`). After
  that, instant. Messages arriving BEFORE the key sit in a pending queue
  (`decryptor.cpp`) and decode the moment the key lands.

**Key words:** public key = padlock, private key = key that opens it, Olm envelope,
Megolm sender-key, pending-event queue.

## Lesson 7 — Trust: verification, cross-signing, and backup

**Core idea:** The whole "only I can read" promise depends on the padlock in the
phone book really being YOUR device's padlock. If the server were evil, it could
swap in its own padlock and open your mail. Verification is how you check the padlock.

- **SAS verification = the 7-emoji check.** Both devices show 7 emoji; if they match,
  both sides really see the same device keys — you're talking to the Alice device, not
  an imposter (MITM). Green shield = you verified it; red = not trusted yet.
- **Cross-signing** is the *spreadable* form of trust: you sign the other user's master
  key with your own key (`USK`) → `POST /keys/signatures/upload` → their device now looks
  grey-signed, meaning "someone I trust vouches for it." Green = SAS-verified,
  grey = cross-signed, red = unverified.
- **SSSS / key backup (Phase 7):** your room keys are encrypted and stored in a
  safety-deposit box on the server (locked with a recovery passphrase). New device
  logs in → gets a NEW device ID → type the passphrase → room keys import, so you can
  read old history even with a brand-new device ID.
- **The one thing without backup:** no passphrase → the past is gone. New devices can
  still read NEW messages (the current key is re-shared when the new device appears —
  `device_lists:{changed,left}` in /sync triggers `shareRoomKey`).

**Key words:** padlock check (SAS), cross-signature chain, recovery passphrase,
new device ID ↔ imported room keys.

## Lesson 8 — Why crypto code is fiddly: the library "eats your data" traps

**Core idea:** libolm (the crypto library) is VERB-driven: some of its calls
MUTATE the buffer you hand it, in place. You hand it your data believing it's
read-only, and it silently destroys it. We hit 9 of these. Rules:

1. All Olm/Matrix crypto fields are BASE64 in JSON — the lib expects base64. NEVER
   `base64Decode` before an `olm_*` call; it decodes internally.
2. `olm_unpickle_*` decrypts the pickle buffer IN-PLACE — after the call, your
   buffer contains garbage — so you MUST pass a copy:
   `std::string copy = original; sess.unpickle("", copy);`.
3. `olm_sas_set_their_key` decodes the other side's pubkey base64 IN-PLACE — same: pass a copy.
4. Re-calling `olm_*_session(memory)` ZEROS the struct (it re-inits and wipes your
   session). Call it ONCE on fresh memory, then use `static_cast` for later access.
5. `olm_account_unpublished_fallback_key` returns the empty form `{"curve25519":{}}`
   (length > 0) even when NO fallback key exists — detect by content, not length.
- Each of these cost a real debugging cycle. Lesson: crypto bugs are silent and
  invisible to the eye — which is why crypto has automated tests (they fail loudly
  where the app would just show garbage).

**Key words:** base64-decodes internally, in-place mutation → "pass a copy",
one-shot init, empty-form trap.

## Lesson 9 — How bugs get caught: smoke tests, CI, and the "4-time" crash

**Core idea:** The app is protected by three nets: small smoke tests, a full test
suite (`ctest`), and CI that runs everything on every push — including a REAL
homeserver that does encrypted round-trips automatically.

- Every new handler gets a **15-line smoke test** (test_visual.cpp) — build + one
  method call with a fake client. Instant feedback.
- `ctest` = the whole suite; green means "100% tests passed". Run before push.
- **CI spins up a real Synapse server** (a real Matrix homeserver in a container)
  and runs a 3-user encrypted round-trip — register, create encrypted room, share
  room key, decrypt cross-account (test_synapse_e2ee.cpp).
- **Rule #1: push after commit.** The #1 reason "the fix didn't work" is forgetting
  to push — the fix exists only in your editor.
- **Rule: LOG before fixing.** Add diagnostic LOGs → run → analyze → THEN fix.
  Never guess-fix. F12 dumps state on a live app.

**The setClient chain = the crash that came back 4 times (B21, B38, B39, B40).**
Every class that stores the client (`shared_ptr<MatrixClient>`) must have
`setClient()`, and every parent must push it to ALL its children
(`child->setClient(client_)`). When that tiny chain breaks, the child holds a
null/stale pointer → silent crash. It hits context menus and threads first because
those classes are created late (on right-click) and easy to forget. The real fix is
not the one-liner — it's the **audit script** that checks for "MISSING setClient"
before every commit, so the rule can't be forgotten twice.

**Key words:** smoke test, ctest, Synapse CI, push after commit, setClient audit,
LOG-before-fix.

---

## Quizzes (part 2)

### Quiz 6 — Encryption
1. What's a "padlock"? What never leaves the device?
2. Olm vs Megolm: which is the envelope and which is the room key?
3. Why is the first message in an encrypted room slow?
4. The server knows the device ID — why can't it open the envelope?

### Quiz 7 — Trust
1. What does the 7-emoji check really verify?
2. New phone + passphrase → old room keys come back. True/false, and why does it work
   despite a new device ID?
3. What does the chain master key → self-signing key → device key = ? (trust graph)

### Quiz 8 — Crypto traps
1. Should we base64-decode before olm_encrypt? Why?
2. What goes wrong if you hand a pickle to `olm_unpickle` directly?
3. Can you call olm_*session twice on the same memory?

### Quiz 9 — Testing and the rule
1. What is a smoke test?
2. What does `ctest` checking "100% tests passed" tell us?
3. Why does the diagnostics rule say "LOG before fixing"?

---

## Bet Rounds (Pt 2 — after the first 10)

> Same game: real scenario → user guesses → I reveal the code. These 6 rounds were
> actually played in conversation on Aug 5 (the user went 3/4 on the fresh ones).

### Round 11 — "send from a verified device" → correct: C
- The message is encrypted with the ROOM (Megolm) key, not per-device, not plaintext.
  The server catches only the encrypted envelope.
- BUT the B-instinct — "the server can't read it" — is literally correct too:
  the server is the courier that never opens the box. Best answer: C + B's reasoning.

### Round 12 — "the server got hacked" → correct: B
- They get only ciphertext — but they keep an archive forever. If any device's
  private key ever leaks (or you re-login without backup), the whole old history
  decrypts retroactively. With no backup/passphrase, the old history is gone for
  YOU too — the ciphertext stays ciphertext for everyone, even the admin.

### Round 13 — "first message in encrypted room feels slow" → correct: B
- The room key makes a separate delivery trip first (`shareRoomKey` → `/sendToDevice`).
  The pending-event queue holds the message until the key lands, then decodes it.
  Every message after the first is instant.

### Round 14 — the 200-message wall → correct: B (same idea as Round 9)
- The server keeps everything; the app keeps the last 200 in RAM as a sliding
  window and re-fetches on scroll-up. Memory safety valve, not data loss.

### Round 15 — "the crash that returned 4 times" (B21/B38/B39/B40) → correct: A
- Not a race, not a leak — a **stale pointer**: a child class never received the
  client via `setClient()` and dereferences null/dead memory.
- It hits context menus and threads because those are created late (on right-click),
  so the propagation is easy to forget. The permanent fix is the audit script that
  greps for "MISSING setClient" before every commit.

### Round 16 — crossword (from a real conversation) — you asked, code answered
- "Does a room have 1 key or N?" → 1 per SENDER, shared with the whole room.
- "New device + passphrase → can I read old history?" → YES: imported room keys
  don't care about the new device ID. Without the passphrase → NO, old history gone.

### The workout (do this to prove it to yourself)
Retell the whole encrypted-room story in your own words: create room → invite →
send first message → friend decrypts. Include: padlock, phone book, 7-emoji check,
Megolm key, envelope. Then compare against Lesson 6-7 and see what you missed.

---

## Your personal checklist (how to keep learning from YOUR app)

> These are things only you can do — the AI coder can't. One per day is enough.

### Daily (5 minutes)
- [ ] Read one lesson (or one quiz) and answer the questions aloud.
- [ ] Notice one thing in the app you didn't understand yesterday, and guess why it
      works that way BEFORE reading the code.

### Weekly (one thing)
- [ ] Play one Bet Round from memory — guess first, THEN look at the answer.
- [ ] Run `ctest --test-dir build` and check it says "100% tests passed".
- [ ] Press F12 on the running app and look at the state dump — read ONE number.

### Try-it-yourself experiments (safe, your own device)
- [ ] Log in on a second device → verify it with the 7-emoji check (green shield).
- [ ] Set up the SSSS passphrase, then re-login → old history comes back.
- [ ] Make a backup copy of session.db, then delete it → see what you lose
      (you know why now — Lesson 2 + 7).
- [ ] Watch the "first message in an encrypted room" be slow, then the second
      message be instant — you're watching the key delivery trip.
- [ ] Fix nothing, just LOOK: open the timeline of a room with >200 messages and
      scroll up — you're watching the sliding window re-fetch.

### The real test (the pencil test)
- [ ] Explain the encrypted-room story to someone else. If you can't do it in 3
      minutes without notes, redo the workout round.
