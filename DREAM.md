# Progressive Chat — Dream Client Reference

Everything to remember when designing the Matrix client that puts YOU in control.

---

## Philosophy

**Progressive = клиент для людей, которые КОНТРОЛИРУЮТ СВОИ ДАННЫЕ.**

Not "beautiful" (FluffyChat).
Not "corporate" (Element).
Not "TUI" (iamb).

But: data sovereignty + maximum performance + keyboard-driven UX.

> "You control everything. The server is just a pipe."

---

## Unique Features — Nobody Has These

- **simdjson /sync parser** — 50-200x faster than any other client
- **Redacted recovery** — deleted messages restored from local SQLite
- **Custom key bindings** — user assigns any key to any action
- **BYOK AI** — your own API key, no data leaves your machine
- **Pre-send Message Transformer** — AI rewrites message before sending (styles, languages, custom prompts)
- **Feature Request Assistant** — AI asks clarifying questions, generates structured spec
- **Vocabulary** — select word, save with translation + context, spaced repetition, export to Anki
- **Local FTS5 search** — instant full-text search across all rooms
- **Chunked file upload** — bypass server size limits
- **Whisper.cpp local** — voice transcription OFFLINE, no API key
- **Local statistics dashboard** — activity charts, peaks, patterns (all local)
- **Saved Messages** — private chat with yourself (like Telegram)
- **Quiet send** — send without notification
- **Screenshot + annotations** — built-in screen capture with arrows/text/blur
- **3D model viewer** — .glb/.stl/.obj directly in chat
- **Scheduled send** — compose now, send later
- **Chat folders** — group rooms: "Work", "Friends", "Noise" (local)
- **Ghost messages** — disappear after N seconds on recipient side
- **Message bookmarks** — ⭐ messages, Ctrl+B to jump between them
- **Snippets / templates** — `/snippet meeting` inserts prepared text
- **Regex room filters** — hide/highlight messages by pattern
- **Terminal embed** — ` ```run\nls -la\n``` ` → execute locally, show ANSI output
- **Sound trigger by word** — "urgent" in message → siren sound
- **LAN sync** — sync state between own devices without server
- **P2P file transfer** — WebRTC data channel between clients
- **Room heatmap** — GitHub-style calendar of daily activity
- **Smart copy** — `[Alice, 14:30]: Hello` instead of just `Hello`
- **Message timeline scrubber** — drag slider to scroll through days at light speed
- **Piper TTS "Read aloud"** — offline text-to-speech
- **Language auto-detect** — pie chart of your language mix
- **Word of the Day** — from your vocabulary, in status bar
- **Streak counter** — 🔥 14 days active (admin view for community rooms)
- **Rick Roll detector** — auto-reply when someone sends that link
- **Snake in status bar** — while sync is running
- **Konami code easter egg** — hidden Matrix-green terminal theme
- **Invisible mode** — read without sending read receipts or typing indicators
- **Double-click = ❤️** — instant reaction
- **Birthday icon** — 🎂 on messages from users whose birthday it is
- **Avatar gallery** — multiple avatars like Telegram
- **Avatar shape** — circle / rounded square / square toggle
- **User Card** — custom fields: Race, Age, Level, Skills, Bio, Links
- **Custom nickname color** — user picks, overrides hash-color
- **Status message** — emoji + text under name
- **Achievement badges** — in UserCard, from local stats
- **XP system** — +1 per message, +5 per reaction, +10 per encrypted
- **Levels** — «Newbie» → «Chatter» → «Encrypted Veteran» → «Matrix Sage»
- **Plugin system** (QuickJS) — anyone can write features in JavaScript

---

## Architecture

```
progressive-shared/               ← new shared library (Qt-free C++20)
  ├── llm/                        ← from Agora
  │   ├── provider.hpp/cpp        (OpenAI, Anthropic, Ollama, custom)
  │   ├── stream.hpp/cpp          (SSE streaming)
  │   └── tools.hpp/cpp           (memory, web search, vocabulary)
  ├── audio/
  │   └── recorder.hpp/cpp        (microphone for voice messages)
  └── screenshot/
      └── capture.hpp/cpp         (screen capture + annotations)

progressive-core/                 ← Matrix core (Qt-free, C++20)
  ├── matrix_client               (HTTP + Matrix API)
  ├── sync_engine                 (/sync loop + backoff)
  ├── fast_sync                   (simdjson zero-copy parser)
  ├── session_store               (SQLite persistence)
  ├── crypto/                     (libolm: OlmAccount, MegolmStore, Decryptor)
  ├── event_bus                   (plugin event system)
  ├── plugin_host                 (QuickJS sandbox)
  ├── plugin_api                  (JS bindings for progressive.*)
  └── notifications               (desktop notifications)

progressive-gui/                  ← Qt6Gui + QPainter widget toolkit
  ├── ui/
  │   ├── main_window             (tiling layout, multi-pane)
  │   ├── chat_view               (self-contained chat widget)
  │   ├── timeline_delegate       (bubble rendering)
  │   ├── room_list_delegate      (room list with invites)
  │   ├── space_panel             (space icon sidebar)
  │   ├── user_card_dialog        (profile card viewer)
  │   ├── user_card_editor        (profile card editor)
  │   ├── avatar_gallery_widget   (multi-avatar manager)
  │   ├── vocabulary_dialog       (word list + spaced repetition)
  │   ├── feature_assistant       (AI clarifies feature requests)
  │   ├── transform_preview       (pre-send message preview)
  │   ├── plugin_settings_dialog  (plugin enable/disable/logs)
  │   └── theme                   (design tokens + dark theme)
  └── resources/

progressive-tui/                  ← FTXUI terminal frontend (future)
  └── same core, terminal rendering

progressive-daemon/               ← headless mode (future)
  └── core 24/7, GUI/TUI connect via D-Bus

progressive-android/              ← shared C++ core on Android (future)
  └── same progressive_native, JNI bridge
```

---

## Plugin System (QuickJS)

### Principles

- All fancy/experimental features are **opt-in plugins**, hidden in Settings
- User must manually enable each
- One `.js` file + one `.plugin.json` manifest
- Plugin never has access to: filesystem, network, other users' data
- RAM limit: 10 MB per plugin, CPU timeout: 100ms per call

### Plugin API Surface

```javascript
// Events
progressive.on("message_received", cb)
progressive.on("message_sent", cb)
progressive.on("room_switched", cb)
progressive.on("reaction_added", cb)
progressive.on("startup", cb)
progressive.on("shutdown", cb)
progressive.on("ui_tick", cb)          // every 1 second
progressive.on("window_focused", cb)
progressive.on("window_unfocused", cb)

// UI modifications
progressive.statusBar.addWidget(name, html)
progressive.timeline.addMessageAction(label, icon, callback)
progressive.timeline.addRoomAction(label, icon, callback)
progressive.toolbar.addButton(name, icon, callback)
progressive.sidebar.addSection(name, html)

// Data
progressive.sql.query("SELECT ...", [...])
progressive.sql.exec("INSERT ...", [...])
progressive.config.get("key")
progressive.config.set("key", "val")

// Actions
progressive.sound.play("/path/file.ogg")
progressive.clipboard.copy("text")
progressive.notify("Title", "Body")
progressive.log("debug info")

// Message hooks
progressive.messages.get(eventId)
progressive.messages.search("keyword")
progressive.messages.beforeSend(callback)  // TRANSFORM hook!
```

---

## Design Tokens

```
Colors (dark theme):
  incomingBubble:  #2a2a3e
  outgoingBubble:  #0f3460
  textColor:       #f0f0f0
  timeColor:       #888
  systemTextColor: #777
  rowBgDark:       #141414
  rowBgLight:      #1e1e1e
  selectedBg:      #325082
  reactionBg:      #2a2a2a
  reactionBorder:  #3a3a3a
  accent:          #2a82da (user-configurable)

Sizes:
  avatarSize:      36px (circle or rounded square)
  bubbleRadius:    12px
  bubblePadding:   10px
  margin:          8px
  gap:             8px
  sameSenderGap:   2px
  padTop:          6px
  padBottom:       4px
  timeRowH:        14px

Fonts:
  body:            10pt sans-serif
  sender:          10pt bold
  timestamp:       9pt #888
  badge:           8pt bold white

Bubbles:
  Incoming:  left, avatar left, #2a2a3e
  Outgoing:  right, avatar right, #0f3460
  System:    center, gray italic, no bubble
  Emote:     no bubble, italic gray
  Group:     first: top-radius 12 + bottom-radius 0
             middle: all radii 0
             last: top-radius 0 + bottom-radius 12
             only: all radii 12
```

---

## Tomogichi + Agora Integration in Progressive

### What is Tomogichi

Tomogichi is a habit-tracking RPG with 4 characters (Riff, Reef, Pitch, Rain):
- Each character has skills, levels, and mood
- User tracks: tasks, diary, mood, calendar, challenges
- Characters have personalities — they react to what user does

### What is Agora

BYOK LLM client connecting to OpenAI/Ollama/Anthropic. AI can:
- Chat with user
- Execute tools (memory, web search, shell)
- Interact with Tomogichi via file bridge (Teletraan)

### Integration Vision

```
                         Progressive Chat
    ┌────────────────────────────────────────────────────┐
    │                                                    │
    │  Matrix Rooms          Tomogichi Panel             │
    │  ┌──────────┐         ┌────────────────────┐      │
    │  │ #general │         │ 🧝 Riff  Lv.12 😊  │      │
    │  │ #random  │         │ 🏗️ Reef  Lv.8  😴  │      │
    │  │ #dev     │         │ ⚡ Pitch Lv.15 😤  │      │
    │  └──────────┘         │ 🌧️ Rain  Lv.10 😐  │      │
    │                       └────────────────────┘      │
    │                                                    │
    │  AI Assistant (LLM from Agora)                    │
    │  ┌────────────────────────────────────────────┐    │
    │  │ AI: "Riff seems unhappy. Maybe do a        │    │
    │  │     10-min design practice?"               │    │
    │  │                                            │    │
    │  │ You: "ok"                                  │    │
    │  │                                            │    │
    │  │ AI: "✅ Logged practice for Riff.          │    │
    │  │      +25 Design XP. Mood improved."        │    │
    │  └────────────────────────────────────────────┘    │
    │                                                    │
    │  Saved Messages ← daily questions from Tomogichi   │
    │  "What did you practice today?"                    │
    └────────────────────────────────────────────────────┘
```

### How it works (technically)

```
Progressive reads Tomogichi state directly from its SQLite DB:
  ~/.local/share/tomogichi/tomogichi.db
    ├── characters (Riff, Reef, Pitch, Rain)
    ├── skills (each character's skills + levels)
    ├── calendar (events, deadlines)
    ├── tasks (todo items)
    ├── mood_log (daily mood entries)
    └── diary (daily journal entries)

Progressive AI (shared LLM from Agora) can:
  1. Read tomogichi state → understand context
  2. Generate coaching messages → send to Saved Messages
  3. Read diary entries → give feedback
  4. Log practices → call tomogichi CLI: `tomogichi diary "Did 10min design"`
  5. Track mood → `tomogichi mood 4`
  6. Create tasks → `tomogichi task "Finish project X" due friday`
  7. Emergency protocol → detect neglect, compassionate intervention

Tomogichi CLI commands (executed by Progressive):
  tomogichi diary "text"           # add diary entry
  tomogichi mood 5                 # log mood (1-5)
  tomogichi task "text" due DATE   # create task
  tomogichi export agora           # export full state for AI context
  tomogichi challenge "name"       # start a challenge
```

### Plugin: Tomogichi Panel

```json
{
  "name": "Tomogichi Bridge",
  "version": "1.0",
  "permissions": ["read_sqlite", "execute_local", "notify"],
  "config": {
    "tomogichi_db_path": "~/.local/share/tomogichi/tomogichi.db",
    "tomogichi_cli_path": "/usr/local/bin/tomogichi",
    "ai_check_interval_minutes": 30,
    "emergency_check_interval_minutes": 5
  }
}
```

### User experience

1. User enables Tomogichi plugin in Settings
2. Left sidebar gets a "Tomogichi" space with characters
3. Each character shows: avatar (custom image), name, level, mood emoji
4. AI assistant occasionally sends messages to Saved Messages:
   - "Riff's mood dropped. Last practice was 3 days ago."
   - "Reef leveled up! 🎉 New skill unlocked."
   - Emergency: "Rain hasn't been cared for in 5 days."
5. User replies to AI in Saved Messages — AI logs actions to Tomogichi
6. All state stays local. No data sent to Matrix server.

### Migration from Teletraan

| Teletraan (current) | Progressive (future) |
|---|---|
| bridge.py HTTP server for Firefox | Direct SQLite read — no bridge needed |
| File-based actions (JSONL) | Direct CLI calls |
| Browser localStorage config | SQLite config in Progressive |
| Web Speech API for TTS/STT | Piper TTS + Whisper.cpp local |
| Separate HTML page | One app, one window |

---

## Competitors — Weak Spots (Opportunities)

```
Element Web:    400-800 MB RAM, slow start, JS parser
Element X:      No Spaces, only OIDC, mobile-only
Cinny:          SDK rewrite in progress, feature freeze
FluffyChat:     Flutter slow on weak devices
Nheko:          Desktop-only, utilitarian UI, complex build
Fractal:        Linux GNOME-only, few features
NeoChat:        KDE-only, heavy dependencies
SchildiChat:    Inherits ALL Element Web problems
iamb:           TUI only (0.1% users), Vim-barrier
gomuks:         Split architecture, experimental terminal
Thunderbird:    Matrix secondary to email
Singularity:    134 commits, too early
```

---

## Build Rules

```
● AI must compile BEFORE every push
● Command: cmake --preset desktop && cmake --build build -j$(nproc)
● Never commit without successful build
● Ignore: warnings in submodule (rainbow.cpp, password_validator.cpp)
● Ignore: sqlite3.c warnings
● CRITICAL: errors in fast_sync.cpp (simdjson API misuse)
● CRITICAL: errors in any progressive_core file
```

---

## Target Platforms

```
PinePhone (ARM A53, 2GB):    goal <80 MB RAM GUI, <25 MB daemon
PineTab 2 (ARM A55, 4-8GB):  goal <120 MB RAM GUI
Desktop Linux (x86_64, 8+GB): goal <150 MB RAM GUI
Android (via shared core):    future
```

---

## Markdown Engine

Replace `progressive::markdownToHtml` with **cmark-gfm** (C library):
- GitHub Flavored Markdown: tables, strikethrough, task lists, autolinks
- CommonMark-compatible
- 300 KB .so, compiles in 3 seconds on ARM
- Nheko already uses cmark — proven on ARM

---

## Feature Priority

### NOW (v0.12-v0.15) — Basic completeness
```
[x] Session refresh (M_UNKNOWN_TOKEN fix)
[x] Bubble corner merging
[x] Invitations redesign
[x] Read markers auto-send
[x] Typing indicators (receive)
[x] Reply preview
[x] E2EE Megolm persistence
[x] Design tokens
[ ] @mention autocomplete
[ ] Typing indicators (send)
[ ] File/audio cards in timeline
[ ] Scroll-to-bottom floating button
[ ] Draft save per room
```

### NEXT (v0.16-v0.18) — Differentiators
```
[ ] Custom key bindings
[ ] Ctrl+K room switcher
[ ] Double-click = ❤️ reaction
[ ] Smart copy (Ctrl+C → [Name, Time]: text)
[ ] Date dividers in timeline
[ ] Format toolbar above input
[ ] Clipboard image paste → auto-upload
[ ] Redacted recovery UI
[ ] Markdown → cmark-gfm
[ ] Custom accent color
[ ] Chat wallpaper option
[ ] Message density (Comfortable/Compact/Super Compact)
[ ] Avatar shape toggle (circle/square/rounded)
[ ] Invisible mode toggle
```

### LATER (v0.19+) — Unique
```
[ ] Plugin system (QuickJS)
[ ] Space-oriented navigation
[ ] Message bookmarks
[ ] Snippets/templates
[ ] Scheduled send
[ ] Saved Messages room
[ ] Quiet send
[ ] Multi-account UI
[ ] Room heatmap
[ ] Streak counter (admin feature)
[ ] Word counter in status bar
[ ] Snake in status bar
[ ] Sound triggers by words
[ ] Ghost messages
[ ] Message timeline scrubber
[ ] AI: LLM providers (shared with Agora)
[ ] AI: Pre-send Message Transformer
[ ] AI: Feature Request Assistant
[ ] Vocabulary + spaced repetition
[ ] TTS "Read aloud" via Piper
[ ] Tomogichi integration plugin
```

### ARCHITECTURE (parallel track)
```
[ ] Plugin host (QuickJS)
[ ] Plugin API bindings
[ ] Core 100% Qt-free
[ ] ChatView refactor (self-contained widget)
[ ] Tiling layout (multi-pane)
[ ] TUI frontend (FTXUI)
[ ] Daemon mode + D-Bus API
[ ] Image packs (MSC2545)
[ ] Chunked file upload
[ ] Local FTS5 search
[ ] Whisper.cpp integration
[ ] Flatpak packaging
```

---

## Technical Debt (Don't Forget)

```
[ ] Megolm persistence — hand-rolled JSON, need edge case review
[ ] Olm session persistence — not yet implemented
[ ] mark_keys_as_published — works but wrapper could be cleaner
[ ] Pending event retry on room_key arrival — integration missing
[ ] m.direct account data — not parsed
[ ] Backward pagination UI — API ready, UI not triggering
[ ] sizeHint reactionH — now in BubbleLayout, good
[ ] sameSender: already checks system messages, but need re-verify
[ ] README outdated (says v0.1.0 / Phase 0, actually v0.15.0 / Phase 5)
[ ] No CI
[ ] No Flatpak/package
[ ] Only build script is build-pt2.sh (PineTab only)
[ ] closeEvent doesn't call QMainWindow::closeEvent (minor)
[ ] m.reaction parsing in dirty file uses manual string search (frAGILE)
```

---

## Features NOT to Do (Crossed Out)

```
✗ Bots — Element already has widgets. Complex, not unique.
✗ Calendar — Thunderbird does it. Separate product.
✗ Email — IMAP/SMTP from scratch. Separate product.
✗ Diff for edited — Element has it.
✗ VoIP/WebRTC — Huge effort. GStreamer or MatrixRTC. Much later.
✗ Sliding Sync — Experimental. simdjson already fast.
✗ Polls — Element has them. Complex spec.
✗ SSO — Only password login for now. Desktop/Linux niche.
✗ Night Owl bonus — false motivation, harmful
✗ Emoji-only mode — removed
✗ Random compliment on startup — removed
```

---

## Answers to Key Questions

**Q: Why not TUI?**
A: TUI = 0.1% of users. GUI already has 1300+ lines. TUI can be added LATER
as second frontend on same core. Don't throw away the GUI.

**Q: Why not QML?**
A: QWidgets can look great (Telegram Desktop). QPainter + custom delegate
give full control. QML = full rewrite.

**Q: What about Agora?**
A: Extract shared code (LLM provider, audio, screenshot) into progressive-shared.
Both Agora and Progressive use it. Don't merge.

**Q: What about Teletraan?**
A: When Progressive gets AI + Tomogichi plugin, Teletraan becomes unnecessary.
That's Phase 4+, not now.

**Q: 145 MB RAM — is that OK?**
A: Yes. Nheko: 100-180. Element: 400-700. Can drop to ~100 with optimizations.
But prioritize features over RAM now.

**Q: Why switch from HTML to cmark-gfm for markdown?**
A: Current progressive::markdownToHtml is basic (bold, italic only).
cmark-gfm gives tables, strikethrough, task lists, autolinks. 300 KB, C, compiles instantly.

**Q: How to reach <80 MB RAM on PinePhone?**
A: SQLite cache_size -500, malloc_trim after every sync, lazy room state unload,
timeline truncate to 30 events for inactive rooms. Achievable in 2-3 sessions.

**Q: Why QuickJS for plugins, not Python/Lua?**
A: QuickJS: 300 KB .so, C API, perfect sandbox (no fs, no net),
compiles in 3 seconds on ARM. Python: 2 MB+, complex binding. Lua: tricky stack API.

**Q: How are reactions drawn — inside or outside bubble?**
A: Inside bubble for middle-of-group messages (don't break the group).
Outside bubble for first/last/only messages (natural spacing).
```

---

v0.15.0 — July 2026
