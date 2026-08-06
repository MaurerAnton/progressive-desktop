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
| Jul 31 | **UI polish sprint.** 5min bubble corner merging (B27), Ctrl+K room switcher, reaction pill toggle (DREAM #56), member count in invite preview (B17-B19), clickable file/audio (B31), scroll anchoring (B29), context menu gating (B26), thread root count (B35). | 9 |
| Aug 1 | **Phase 2 SAS verification COMPLETE + live-Synapse CI.** m.sas.v1 state machine + crypto + dialog + handler + two-manager protocol test (40+ commits). 17 spec-compliance bugs fixed. `synapse-e2ee.yml` runs a real Synapse: cross-account encrypted room round-trip GREEN. CI build fixes. | 40+ |
| Aug 2 | **Cross-signing (Phase 6).** MSK/USK/SSK ed25519 keygen + setup + device signing + UIA-aware publishing. | ~10 |
| Aug 3 | **Phase 6 tail + E2EE hardening.** Trust computation, SAS MSK exchange, cross-user cross-signing, UI device shields, cross-signing reset. E2EE bootstrap extracted to core (X1 phase 4). | ~10 |
| Aug 4 | **Phase 7 (SSSS + key backup) + X1 + 4 UI bugs.** /room_keys backup, SSSS secret sharing, recovery key; X1 engine extraction Phases 1-6 (Qt-free engine_types/state/applier); four reported PineTab UI bugs fixed; reply + file/audio sync-path fixes, ~3s→20s poll. | 20+ |
| Aug 5 | **E2EE completeness + crashes.** Encrypted media both ways, Element-parity key requests, cross-client SAS, close crash fix, Olm recovery completeness, encrypted menu reactions/edits, m.replace handling, identity-reset heals broken Olm chains. | 10+ |
| **Total** | **~490 commits** | |

---

## By the Numbers

| Metric | Value |
|---|---|
| Commits | ~490 (16 days) |
| MainWindow reduction | 2772 → 220 lines (-92%) |
| Files after mega prompt split | +7 new, -1000+ lines of fat |
| Shared_ptr migration | 39 files (MatrixClient + SessionStore) |
| E2EE bugs fixed | 30+ (Bug A + Bug B + self-echo + spec compliance + libolm quirks + 17 SAS) |
| Bugs fixed (total) | 45+ (B1-B40 + E2EE) |
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
 - [x] B26 — context menu irrelevant options (fixed Jul 31: gating on preconditions — pin/unpin, reaction, edit, reply disabled when N/A; room_context_menu.cpp:269-284)
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

## Bug Tracker (source of truth — consolidated from DREAM.md, Aug 1)

> All active bug lists now live here. DREAM.md mirrors the active list; this is authoritative.

### Active — Critical (blocking alpha)
```
[x] Thread: reply OK button — message not appearing anywhere after sending — DONE (user verified Aug 2)
[x] Thread: root message emoji/smile disappears after room switch (loadHistory doesn't set thread flags) — DONE (user verified Aug 2)
[x] Thread: threadReplyCount never increments via sync path (use-after-move in appendTimelineForRoom) — DONE (user verified Aug 2: was about other users' replies; now increments correctly)
[x] Thread: first reply appears twice in thread view (echo temp ID vs sync real ID) — DONE (echo uses r.data, thread_handler.cpp:187; user verified Aug 2)
[ ] Thread: reply count grows +1 per reaction click IN THREAD VIEW — clicking a reaction on a thread reply
    increments threadReplyCount by 1 each click; going back to chat reloads and shows the correct number.
    Suspected: reaction sync re-delivers the reacted-to reply with an empty eventId, evading seenIds_ dedupe
    (timeline_model.cpp:247-248,270-277), so it's appended as a "new" reply → +1. NEEDS DIAGNOSTIC LOGs
    (per PLANNER #9): log eid + eventIdEmpty in appendBackBatch; confirm before fixing. Do NOT guess-fix.
[x] Reply from Element renders inconsistently — root cause CONFIRMED (Aug 3): reply metadata
    (m.relates_to.m.in_reply_to) was only parsed in the HISTORY paths (room_data_loader.cpp:118-121,
    140-144; room_handler.cpp:227-231), never in the LIVE-SYNC path fastEventToDisplayed
    (room_store.cpp:318-397), so replies arriving via /sync rendered as plain messages with Element's
    literal "> @user:server  original" fallback quote visible in the bubble (isReply=false).
    DONE (Aug 4 evening batch, checklist.md): extractReplyToId/extractThreadRootId now called in
    sync_applier fastEventToDisplayed and the "> <@user:...>" fallback is stripped when isReply.
[ ] Invite: reject fails with M_FORBIDDEN "duplicate auth_events for m.room.member" (need diagnostic LOGs)
[ ] Image: images don't render in timeline or viewer — downloadMedia may fail silently (need diagnostic LOGs)
[x] File/audio download dead — root cause CONFIRMED (Aug 2): mxcUrl was only parsed for
    m.image/m.video, NOT m.file/m.audio (room_store.cpp:376 `if (msgtype=="m.image"||"m.video")`).
    So file/audio cards rendered (painter draws card from msgtype only, timeline_painter.cpp:303)
    but clicks silently no-op'd because timeline_delegate.cpp:217 requires !mxcUrl.isEmpty().
    DONE (Aug 4 evening batch, checklist.md): m.file/m.audio url (+filename) parsed in all three
    paths (sync in sync_applier, load-more, history) — superseded by Aug 5 encrypted-media.
[ ] Room creation: no "+ New room" action for group rooms, no encrypted room creation
[x] Multi-account UI — DONE (account_switcher.cpp addAccount/switchAccount/logout wired via combo in main_window.cpp:285; LoginDialog flow verified Aug 2)
[x] Copy messages: "Copy text" context action — DONE (room_context_menu.cpp:304)
[ ] Event source viewer: cannot see raw event JSON (sender_id, body, type, etc.) like Element
[x] Message delivery ~3s delay — CONFIRMED (Aug 4): the ~3s was the long-poll timeout. FIXED:
    sync poll default 3000→20000ms (checklist.md Aug 4 evening batch).
```

### Active — Medium
```
[ ] Markdown: cmark-gfm needed — lists/tables/code blocks show as raw markdown
[ ] DM: always creates new room, doesn't check m.direct for existing
[x] Chat logging duplication (DEBT-002) — DONE (verified Aug 2): ChatLogger is now a single class
    (src/ui/chat/chat_logger.cpp), owned only by ToolbarHandler (chatLogger_, toolbar_handler.hpp:83),
    passed to ChatView via setChatLogger() (chat_view.hpp:23, main_window.cpp:176). room_handler no
    longer has any logging code (0 hits). Invisible to users — pure code-maintenance dedup.
[ ] Desktop notifications — basic implementation, blue square icon
[ ] Notification loop: only ONE room notified per sync (`break` at sync_response_handler.cpp:87) —
    per sync cycle exactly one popup (first unread room), so N unread rooms ≠ N popups in one
    batch; multiple rooms only appear across successive syncs (user observed 2 rooms = 2 syncs).
    Also no dedup — a room that stays unread-and-first re-notifies every sync. @mention body text
    IS implemented (:83-84). Compare with other clients (per-room, mention-only, grouping).
[ ] Error visibility: errors show in status bar line where sync stats normally appear, no dialog/log panel
[ ] test_gui_phase4 duplicates RoomListModel (test code only, not blocking)
[ ] RoomListDelegate::paint const_cast (DEBT-012, works but ugly)
[ ] Missing emoji font resource for PineTab without system emoji font (DEBT-016)
[ ] Typing indicator doesn't refresh — receiving m.typing is parsed (fast_sync.cpp:114-130) and
    stored on the upsert record (room_store.cpp:171), but RoomListModel::upsertRoom
    (room_list_model.cpp:60) never copies typingUsers into the existing room and never emits
    dataChanged for it → sidebar "X is typing..." likely only shows on room create, not live.
    Also NO typing UI in the chat/timeline view at all (zero hits in src/ui/chat, src/ui/timeline).
    Fix scope: upsertRoom copy+emit for typingUsers + optionally in-chat indicator.
```

> **Next diagnostic pass (one AI-coder task) — remaining runtime-only bugs need LOGs-before-fix:**
> (1) images don't render — log downloadMedia/httpGet for the image mxcUrl (Aug 5 media rework
> added encrypted + fallback paths — confirm at runtime);
> (2) thread reply count +1 per reaction in thread view — log eid + eventIdEmpty in appendBackBatch.
> Per PLANNER #9: add LOGs first, analyze, THEN write the fix. Do NOT guess-fix.
> (The Reply-from-Element + file/audio fixes from the diagnostic note below are DONE — Aug 4 evening batch.)

### Discussed Wishes (not yet prioritized)
```
[ ] Keyboard navigation — full Nheko-level shortcuts (Ctrl+K, Alt+↑↓, Escape, Tab chain, Up-to-edit)
[ ] Theme — JSON token manifest import/export (Element-level custom themes)
[ ] Theme — per-element color customization via token overrides
[ ] Polls/tables — MSC3381 m.poll.start / m.poll.response (deferred to beta)
[ ] E2EE file encryption — encrypted room file uploads currently bypass encryption (DEBT at chat_view.cpp:279)
[ ] AppImage + Flatpak — binary releases for alpha (packaging is v1.0 per REFERENCE)
[ ] Multi-account polish — UI for second account DONE (addAccount via combo → LoginDialog)
[ ] Multi-account verification — prove it actually works
[ ] Forget room — M_UNKNOWN "user is in room" — user must Leave first, then Forget
[ ] W15 — search word (local FTS5 full-text index, cheap since FTS5 already planned)
```
> Duplicates removed Aug 4: Room creation, Message info panel, Copy messages, Markdown tables,
> Image preview, Invite M_FORBIDDEN, Error visibility, Markdown cmark-gfm — all already tracked
> in the Active sections above.

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

Jul 31: UI polish — 5min bubble corner merging (B27), Ctrl+K room switcher with
        filter + arrow nav, reaction pill toggle + self-double-click guard + strip
        count (DREAM #56), member count in invite preview from invite_state (B17-B19),
        clickable file/audio events (B31), scroll anchoring + cached BubbleLayout (B29),
        context menu gating on preconditions (B26), threadRootId on /relations replies (B35).

Aug 1: E2EE Phase 2 — SAS verification COMPLETE (40+ commits):
        - m.sas.v1 state machine (verification.cpp, 476L) — 12 states, transaction
          tracking, commitment + MAC verification, StateChangedFn per transition
        - OlmSAS crypto (sas.cpp) + 64-emoji table (sas_emojis.cpp, 7 overlapping
          13-bit windows) ported from submodule
        - VerificationController (send side) + VerificationHandler (UI) +
          SasVerificationDialog (7-emoji match/mismatch/cancel)
        - RoomMembersDialog right-click 'Verify…' + PrefsDialog 'Your devices'
        - Two-manager protocol test (test_e2ee_verify_protocol.cpp) — full
          request→done flow, emoji match, corrupted-MAC cancel path
        - SAS crypto roundtrip test (test_e2ee_sas.cpp)
        - 17 spec-compliance bugs fixed: base64 in-place (quirk #9), MAC info
          7-part format, commitment computed+verified, MAC-before-Done, role
          inversion, curve25519-hkdf-sha256, from_device everywhere, emoji table
          64th entry, m.key_mismatch cancel instead of 10min timeout, etc.
        - CI: live-Synapse E2EE integration test (.github/workflows/synapse-e2ee.yml)
          — registers 2 users, encrypted room, room-key share, cross-account decrypt.
          GREEN. Local test skips gracefully when no server.
        - CI build fixes: <unordered_set> include (room_list_model.hpp), bare
          nullptr → QPointer<MainWindow>() (test_visual.cpp, Qt 6.4), ui_widgets↔
          ui_dialogs static-lib cycle (CMake repeated libs, GNU ld).

Aug 2-3: Cross-signing (Phase 6) — MSK/USK/SSK ed25519 keygen/sign/verify, UIA-aware
        publishing, Phase 6 tail (trust computation, SAS MSK exchange, cross-user USK
        cross-signatures, device shields, reset flow). Live-Synapse CI covers the chain.

Aug 4: Phase 7 (SSSS + key backup) — /room_keys backup (create/upload/restore/delete),
        recovery key (base58+parity), SSSS secret sharing (HKDF+AES-256-CBC+HMAC), X1
        engine extraction Phases 1-6 (Qt-free engine_types/room_state/timeline_state/
        sync_applier), 4 PineTab UI bugs, reply+file/audio sync fixes, ~3s→20s poll.

Aug 5-6: E2EE completeness — encrypted media both ways (m.encrypted v2), Element-parity
        key requests (persist/recipients/cancellation), cross-client SAS (Olm-wrapped
        verification), close crash fix (persist vs sync-thread lock), Olm decrypt-failure
        recovery + crash-safe persistence + audit, encrypted menu reactions/edits,
        m.replace handling, outbound Olm session reuse (OTK drain), identity reset heals
        permanently broken Olm 1:1 chains (all-A corrupted identity auto-regeneration).
```

---

## Milestones (Jul 24-27)

| Date | Milestone | Commits |
|---|---|---|
| Jul 23 | Infrastructure, shared_ptr, CI, FluffyChat fix | 20 |
| Jul 24-27 | E2EE Phase 1 complete: Bug A + B fixed, self-echo works, 7 libolm quirks documented | 15+ |
| Jul 28-29 | E2EE Phase 2: Multi-account hardening, shared flag, per-account scoping, OTK count, device_lists, device reset, crash fixes | 11 |

---

*Last updated: Aug 6, 2026*
