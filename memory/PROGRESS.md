# Progressive Chat — Development Progress Tracker

> Personal motivation log. Not shipped. See DREAM.md for vision, AGENTS.md for rules.

---

## Milestones

| Date | Milestone | Commits |
|---|---|---|
| Jul 19 | **Phase 0-4**: Scaffolding, core plumbing, E2EE, UI redesign | 14 |
| Jul 20 | **v0.2→v0.20**: Features + 50+ bug fixes | 33 |
| Jul 21 | **v0.21→v0.25**: Architecture refactoring (MainWindow -92%) | 22 |
| Jul 22 | **Infrastructure**: ThreadPool, Qt-free core, debugging tools | 16 |
| Jul 23 | **Stability**: Shared_ptr, split, daemon-ready, 13+ bug fixes | 20 |
| Jul 24-27 | **E2EE Phase 1**: Bug A (inbound) + Bug B (outbound) + self-echo fixed. 15+ commits. Verified against Element and FluffyChat. | 15+ |
| Jul 28-29 | **Multi-account E2EE**: shared flag, per-account scoping, OTK count tracking, device_lists tracking, device reset, crash fixes. 11 commits. | 11 |
| Jul 30 | **E2EE Foundation + Phase 2 start.** Ed25519 verify, Olm validation, OTK/device sig verify, SAS in progress. Crash fixes (Olm growth, SIGSEGV close, Ctrl+Tab logout, clearAccount). Portability hardening. Color centralization. 8 UI quick wins. Registration token + server presets. Submodule audit (12 REAL, ~30 FAKE). 7-phase E2EE roadmap. | 20+ |
| **Total** | **6 days, 150+ commits** | |

---

## By the Numbers

| Metric | Value |
|---|---|
| Commits | 150+ (in 6 days) |
| MainWindow reduction | 2772 → 220 lines (-92%) |
| Files after mega prompt split | +7 new, -1000+ lines of fat |
| Shared_ptr migration | 39 files (MatrixClient + SessionStore) |
| E2EE bugs fixed | 15+ (Bug A + Bug B + self-echo + spec compliance + libolm quirks) |
| Bugs fixed (total) | 30+ (B1-B38 + E2EE) |
| RAM (8 rooms, sync running) | ~160 MB idle (was 250 MB before Olm session cleanup) |
| Binary size | ~2.8 MB |
| AGENTS.md | 757 → 142 lines (-81%) |

---

## Bug Fixes — Day by Day

### Jul 22 (Day 3 morning)
- [x] B20 — invite connect nullptr
- [x] B21 — all handlers client_ nullptr
- [x] B22 — empty bubbles (sync path)
- [x] B22 regression — loadHistory filter

### Jul 22-23 (Day 3 afternoon/evening)
- [x] B28 — avatars not loading
- [x] B30 — reactions lost on room switch
- [x] B24 — thread emoji lost
- [x] B25 — thread indicator click
- [x] B23 — thread reply root
- [x] B38 — RoomDataLoader client_ null
- [x] Reaction cut — 3 pixel pill
- [x] B37 — M_UNKNOWN_TOKEN loop
- [x] B14 — close button quits
- [x] B30 regression — reaction eid parsing offset

### Still open (as of Jul 27)
- [x] B3 — encrypted room detection (fixed)
- [x] B4 — megolm pending queue (replaced by E2EE recovery chain)
- [x] E2EE Bug A — inbound, can't decrypt in new rooms (FIXED Jul 24-27)
- [x] E2EE Bug B — outbound, others can't decrypt our messages (FIXED Jul 27)
- [x] FluffyChat compatibility — fixed (simdjson parser)
- [ ] B17-B19 — invitation preview
- [ ] B26 — context menu irrelevant options
- [ ] B27 — bubble corner merging
- [ ] B29 — timeline layout instability
- [ ] B31 — files/images not clickable
- [ ] B33 — timing gap (sync before history)
- [ ] B35 — thread reply count (loadHistory)

---

## Feature Progress

### Done
- [x] Login/logout, session persistence
- [x] Room list + invitations
- [x] Chat bubbles, grouping, avatars
- [x] Reactions (quickReact + context menu)
- [x] Threads (view, reply, indicator)
- [x] File/image/audio upload
- [x] Markdown, emoji picker
- [x] E2EE init (olm + megolm setup)
- [x] Read markers auto-send
- [x] Typing indicators (receive)
- [x] Design tokens, dark theme
- [x] simdjson /sync parser (50-200× faster)
- [x] ThreadPool (replaced 56 .detach())
- [x] Qt-free core (QUuid, QDateTime removed)
- [x] 5 static libraries (modular build)
- [x] Crash handler + backtrace
- [x] Debug infrastructure (LOG, ASSERT, TraceFn)
- [x] code_map.json + AGENTS.md + REFERENCE.md
- [x] Shared_ptr migration (no dangling pointer risk)
- [x] GitHub Actions CI

### Next (v0.5)
- [x] E2EE fully working (Bug A + Bug B fixed Jul 24-27, verified against Element and FluffyChat)
- [x] Date dividers
- [ ] Ctrl+K room switcher
- [x] Double-click emoji (❤️ Telegram toggle)
- [x] Configurable history load
- [x] Invisible mode
- [x] Keyboard navigation (Alt+Up/Dn rooms, Ctrl+Up/Dn messages, Ctrl+R reply, Esc close, Ctrl+Tab accounts)
- [x] Smart Copy (Ctrl+Shift+C — sender + timestamp)
- [x] Clipboard paste (Ctrl+V image)
- [x] Server presets + registration token
- [x] Shortcuts reference dialog
- [ ] Context menu cleanup
- [ ] Spaces (hierarchical rooms)

### v0.7 (Safety)
- [ ] SOCKS5/Tor proxy
- [ ] First-run wizard (restricted networks)
- [ ] Screen lock
- [ ] Remote wipe via Matrix
- [ ] Invisible mode
- [ ] Auto-delete / Ghost messages

---

## Full Commit Log

```
Jul 19: Phase 0-4, v0.2.0-v0.5.3 — scaffolding, core, E2EE, emoji, memory diagnostics

Jul 20: v0.6.0-v0.20.4 — lazy loading, images, chat bubbles, 50+ bug fixes,
        design tokens, UI redesign, threads, reactions, avatars, reply preview

Jul 21: v0.21.0-v0.25.0 — state=0 fix, O(1) findRow, simdjson, ThreadPool,
        RoomStore extraction, MainWindow 2772→220 lines, handler extraction

Jul 22: ThreadPool singleton, Core Qt-free, code_map.json, AGENTS.md,
        B20+B21+B22+B28+B30+B24+B25+B23 fixes, mega prompt split,
        B37+B14+B38+reaction cut

Jul 23: Shared_ptr migration (MatrixClient 25 files), CI workflow,
        FluffyChat simdjson parser fix, SessionStore migration

Jul 24-27: E2EE Phase 1 complete — 15+ commits:
        - Bug A (inbound): Olm recovery chain (m.dummy + m.room_key_request)
        - Bug B (outbound): missing room_id in Megolm plaintext
        - Self-echo: import outbound as inbound
        - Stale token: re-call setCryptoContext after token rotation
        - 8 libolm quirks documented in AGENTS.md #6
        - Verified against Element and FluffyChat

Jul 28-29: E2EE Phase 2 — Multi-account hardening (11 commits):
        - 9602df3: crash + garbage account fixes (double-init, recursive mutex)
        - 96c86fc: scope megolm + olm session storage per-account
        - 84d0523: replace global OTK flags with shared flag on OlmAccount
        - 703ec6e: skip OTK upload when shared=true + discard old OTKs
        - 3964ac6: Reset device keys action (clears stale OTKs)
        - 37e981e: deleteDevice JSON field + ThreadPool + UX fixes
        - 8e43412: OTK count tracking + smart generation
        - 46605f7: parse device_lists from /sync
        - 9af0a48: OTK count — guard before discard, sentinel, restore
        - f853d3c: docs update

Jul 30: E2EE Phase 1 (foundation): ed25519 verify, Olm plaintext validation,
        OTK/device key sig verification. Phase 2 SAS in progress.
        Crash fixes: Olm session exponential growth (clear before load),
        SIGSEGV on close (detach→join), Ctrl+Tab logout, clearAccount WHERE,
        auto-cleanup of corrupted sessions.
        Portability: Qt-free core CI guard, crash_handler/memory_stats #ifdef gates,
        rand()→atomic counter, CryptoRandom bridge, sync_engine backup callback.
        Color system: centralization (35 inline hexes→Design tokens),
        ColorSettingsDialog (scrollable, findChild fix), Theme::reapply listeners.
        UI quick wins: date dividers, ❤️ Telegram toggle, Smart Copy,
        history load limit, invisible mode, clipboard paste, shortcuts dialog,
        keyboard nav, server presets + registration token.
        E2EE submodule audit: 12 REAL files, ~30 FAKE boilerplate files identified.
        E2EE roadmap: 7-phase plan documented in docs/E2EE-roadmap.md.
```

---

## Milestones (Jul 24-27)

| Date | Milestone | Commits |
|---|---|---|
| Jul 23 | Infrastructure, shared_ptr, CI, FluffyChat fix | 20 |
| Jul 24-27 | E2EE Phase 1 complete: Bug A + B fixed, self-echo works, 7 libolm quirks documented | 15+ |
| Jul 28-29 | E2EE Phase 2: Multi-account hardening, shared flag, per-account scoping, OTK count, device_lists, device reset, crash fixes | 11 |

---

*Last updated: Jul 30, 2026*
