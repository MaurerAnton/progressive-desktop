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
- Lesson: "the code path exists, but the feature is broken in practice" — why we track bugs.
- Tracked in PROGRESS.md (Critical). Fix deferred by user request.
