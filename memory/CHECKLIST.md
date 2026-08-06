# CHECKLIST.md — What the AI coder does next (ordered)

> The AI coder reads THIS file to know the current task. Work top-down: the first unchecked
> box is the job. Rules always in AGENTS.md; decisions in memory/REFERENCE.md; vision in
> memory/DREAM.md; bug details in memory/PROGRESS.md. PLANNER.md = how to write prompts.
> The root `checklist.md` is the completed-work ledger (keeps done items for reference).
> Last updated: Aug 6, 2026

## How to use

- **One task at a time.** Finish the top unchecked item before the next.
- Each item is a mini-workflow. Do the steps in order; do NOT skip to the fix.
- Mark `[x]` only when the item's "Done when" is verified (build + ctest + PineTab where stated).
- Push after every task (AGENTS.md rule #1 — the #1 cause of "fix didn't work" is forgetting push).

---

## 0. DONE RECENTLY (for orientation — do not redo)

> Phase 1-6 COMPLETE (cross-signing done Aug 3). Phase 7 SSSS + key backup COMPLETE (Aug 4).
> X1 Qt-free engine extraction COMPLETE (Aug 4, Phases 1-6). The four PineTab UI bugs,
> reply-from-Element, file/audio download, and the ~3s delivery delay are all fixed.
> Encrypted media, Element-parity key requests, cross-client SAS, close-crash fix, and
> identity-reset Olm-chain healing all landed (Aug 5-6). 14/14 ctest green.

---

## 1. Bug-zero — remaining runtime bugs (one AI-coder task, LOGs BEFORE fix)

> Per PLANNER #9: add LOGs → run → analyze → THEN fix. Do NOT guess-fix. All are runtime-only.
> Details in PROGRESS.md Active-Critical.

- [ ] Diagnostic LOGs for the remaining runtime bugs
  - [ ] Images don't render in timeline/viewer — log downloadMedia/httpGet for the image mxcUrl
        (Aug 5 media rework added encrypted + fallback paths — confirm at runtime)
  - [ ] Thread reply count +1 per reaction in thread view — log eid + eventIdEmpty in
        appendBackBatch (timeline_model appendBackBatch)
  - **Done when:** each bug has a written root cause in PROGRESS.md; fixes in the next pass.

## 2. Bug-zero — fixes ready to apply (root cause already confirmed)

> These need NO diagnostics — root cause is written. One task = one commit each.

- [ ] Invite: reject fails with M_FORBIDDEN "duplicate auth_events for m.room.member" (diagnostic LOGs first)
- [ ] Room creation: no "+ New room" for groups, no encrypted room creation
- [ ] Event source viewer: no raw event JSON view (like Element)

## 3. Bug-zero — medium bugs (PROGRESS.md Active-Medium)

- [ ] Markdown: cmark-gfm — lists/tables/code blocks render raw (note: tables are Tier-1 scope per DREAM.md)
- [ ] DM: always creates new room, doesn't check m.direct
- [ ] Desktop notifications: basic impl, blue square icon
- [ ] Notification loop: only ONE room notified per sync (`break` at sync_response_handler.cpp:87), no dedup
- [ ] Error visibility: errors only in status bar, no dialog/log panel (see REFERENCE.md error-panel decision)
- [ ] Typing indicator doesn't refresh + no in-chat typing UI (upsertRoom never copies typingUsers/emits)
- [ ] test_gui_phase4 RoomListModel duplicate, RoomListDelegate const_cast (DEBT-012), emoji font (DEBT-016)

## 4. E2EE follow-ups (checklist.md Aug 5 batches — open items)

- [ ] Receive-side m.encrypted media (file:) support + AES media encryption (Element sends/expects
      encrypted media in E2EE rooms)
- [ ] /me emote checks room state (DEBT(E2EE) chat_view.cpp:47)
- [ ] Receive-side GIF (fetchMovie) has no encrypted path
- [ ] Spec-exact session_data KDF (curve25519-aes-sha2) for Element key-backup interop (currently
      crypto_box_seal, self-consistent only)
- [ ] Element interop test — SAS MSK mac extension + cross-signing + backup never validated against
      a real Element client (standing biggest external risk)
- [ ] Multi-account session hygiene — runtime account can differ from bootstrapped one after an
      account switch + pre-refresh; verify the switcher's active-account persistence

## 5. Tier 1 — basic expectations (only after bug-zero)

> Order is fixed. Source: REFERENCE.md Priority Tiers. Do NOT start before 0-4 are clean.

- [ ] Read markdown (cmark-gfm — links/bold/italics/lists; tables are in scope per DREAM message-format)
- [ ] See images in timeline + viewer (runtime-confirm the Aug 5 media work)
- [ ] Chat with and without encryption (plaintext + encrypted rooms)
- [ ] Threads (finish broken thread paths)
- [ ] Room-switcher / context-menu polish
- [ ] Spaces — AFTER the basics above (not before)

---

## Not now (explicitly parked — do NOT start)

- **X1 engine callback wiring** — the engine callbacks (onRoomListChanged/onTimelineChanged) are
  not wired yet (UI-only simplification; trivial when a 2nd frontend lands). Not an obligation.
- **Tier 2** (Tor sync, QuickJS plugins, room view, ~40 DREAM features) — stays vision-only.
- **Android frontend** — both Option A (JNI) and Option B (Qt-for-Android) are candidates, undecided;
  no Android repo changes until after bug-zero → Tier 1 (DREAM.md Android section).
- **Storage manager / drag&drop / notifications-per-room / etc.** — decided in REFERENCE.md but not
  scheduled until Tiers 0-1 are clean.
