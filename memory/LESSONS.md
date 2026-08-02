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
