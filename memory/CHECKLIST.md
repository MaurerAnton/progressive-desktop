# CHECKLIST.md — What the AI coder does next (ordered)

> The AI coder reads THIS file to know the current task. Work top-down: the first unchecked
> box is the job. Rules always in AGENTS.md; decisions in memory/REFERENCE.md; vision in
> memory/DREAM.md; bug details in memory/PROGRESS.md. PLANNER.md = how to write prompts.
> Last updated: Aug 4, 2026

## How to use

- **One task at a time.** Finish the top unchecked item before the next.
- Each item is a mini-workflow. Do the steps in order; do NOT skip to the fix.
- Mark `[x]` only when the item's "Done when" is verified (build + ctest + PineTab where stated).
- Push after every task (AGENTS.md rule #1 — the #1 cause of "fix didn't work" is forgetting push).

---

## 0. E2EE — Phase 7 SSSS + key backup (IN FLIGHT, AI coder active)

> Phase 1-6 COMPLETE (cross-signing done Aug 3). Phase 7 started — `backup_crypto`/`recovery_key`
> files exist in the working tree. Follow `docs/E2EE-roadmap.md` Phase 7 (4-5 sessions).
> NOT fully done until: recovery-key format, Megolm backup encrypt/upload/download/decrypt,
> SSSS secret storage, backup restore on login, and the test-plan.md Phase 10 acceptance steps pass.

- [ ] SSSS + key backup — Phase 7 (see docs/E2EE-roadmap.md:107)
  - **Why:** only way to recover history after re-login; unlocks cross-device secret sharing.
  - **Files:** `src/core/crypto/backup_crypto.*`, `recovery_key.*` (in progress), `docs/E2EE-roadmap.md`
  - **Done when:** `memory/test-plan.md` Phase 10 (key backup + restore) passes, incl. re-login recovery.

---

## 1. Bug-zero — diagnostic pass (one AI-coder task, LOGs BEFORE fix)

> Per PLANNER #9: add LOGs → run → analyze → THEN fix. Do NOT guess-fix. All three are
> runtime-only. Details in PROGRESS.md Active-Critical.

- [ ] Diagnostic LOGs for the 3 runtime bugs
  - [ ] ~3s message-delivery delay — log send start on ThreadPool + HTTP elapsed for sendMessage + sync sent/returned timestamps (PROGRESS.md ~3s entry)
  - [ ] Images don't render in timeline/viewer — log downloadMedia/httpGet for the image mxcUrl (PROGRESS.md Image entry)
  - [ ] Thread reply count +1 per reaction in thread view — log eid + eventIdEmpty in appendBackBatch (PROGRESS.md thread-over-count entry)
  - **Done when:** each bug has a written root cause in PROGRESS.md; fixes in the next pass.

## 2. Bug-zero — fixes ready to apply (root cause already confirmed)

> These two need NO diagnostics — root cause is written in PROGRESS.md. Apply the documented
> fix scope. Fix AFTER the diagnostic pass above (one task = one commit each).

- [ ] Reply from Element renders inconsistently
  - **Fix:** call `extractReplyToId`/`extractThreadRootId` in `fastEventToDisplayed`
    (room_store.cpp:318-397) + re-run on `applyDecryptedEvents` late-decrypt; strip the
    `> <@user:...>` fallback from body when `isReply`. (PROGRESS.md Reply entry)
  - **Done when:** reply from Element in a live-sync room shows the reply UI, not raw `>` text.
- [ ] File/audio download dead
  - **Fix:** parse url (+filename) for m.file/m.audio in BOTH `room_store.cpp` sync path and
    `room_data_loader.cpp` history path. (PROGRESS.md File/audio entry)
  - **Done when:** clicking a file/audio card downloads/opens it.

## 3. Bug-zero — remaining critical bugs (PROGRESS.md Active-Critical)

- [ ] Invite: reject fails M_FORBIDDEN "duplicate auth_events for m.room.member" (diagnostic LOGs first)
- [ ] Room creation: no "+ New room" for groups, no encrypted room creation
- [ ] Copy messages: no copy-to-clipboard from timeline
- [ ] Event source viewer: no raw event JSON view (like Element)

## 4. Bug-zero — medium bugs (PROGRESS.md Active-Medium)

- [ ] Markdown: cmark-gfm — lists/tables/code blocks render raw (note: tables are Tier-1 scope per DREAM.md)
- [ ] DM: always creates new room, doesn't check m.direct
- [ ] Desktop notifications: basic impl, blue square icon
- [ ] Notification loop: only ONE room notified per sync (`break` at sync_response_handler.cpp:87), no dedup
- [ ] Error visibility: errors only in status bar, no dialog/log panel (see REFERENCE.md error-panel decision)
- [ ] Typing indicator doesn't refresh + no in-chat typing UI (upsertRoom never copies typingUsers/emits)
- [ ] test_gui_phase4 RoomListModel duplicate, RoomListDelegate const_cast (DEBT-012), emoji font (DEBT-016)

## 5. Tier 1 — basic expectations (only after bug-zero)

> Order is fixed. Source: REFERENCE.md Priority Tiers. Do NOT start before 0-4 are clean.

- [ ] Read markdown (cmark-gfm — links/bold/italics/lists; tables are in scope per DREAM message-format)
- [ ] See images in timeline + viewer
- [ ] Chat with and without encryption (plaintext + encrypted rooms)
- [ ] Threads (finish broken thread paths)
- [ ] Room-switcher / context-menu polish
- [ ] Spaces — AFTER the basics above (not before)

---

## Not now (explicitly parked — do NOT start)

- **X1 engine extraction** — only when a second frontend (GTK) is wanted or the state layer trips the coder. Not an obligation (REFERENCE.md X1).
- **Tier 2** (Tor sync, QuickJS plugins, room view, ~40 DREAM features) — stays vision-only.
- **Android frontend** — both Option A (JNI) and Option B (Qt-for-Android) are candidates, undecided; no Android repo changes until after E2EE → bug-zero → Tier 1 → X1 (DREAM.md Android section).
- **Storage manager / drag&drop / notifications-per-room / etc.** — decided in REFERENCE.md but not scheduled until Tiers 0-1 are clean.
