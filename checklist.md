# Checklist — pending / remembered items

Tracked so nothing found in the audits gets forgotten. Items are small or deferred;
completed items are removed.

## UI bugs (user-reported Aug 4 — PineTab) — FIXED (d875b0b)
- [x] **Preferences "Your devices" shows "Not logged in."** — loadDevices runs in
      the constructor (prefs_dialog.cpp:304) before setClient() is called; move
      the load into setClient().
- [x] **Room members cannot load members** — ToolbarHandler::onRoomMembers passes
      "" as the room id (toolbar_handler.cpp:185); pass the current room id.
- [x] **Button text clipped vertically** (create backup/backup now/delete/restore
      + the 4 following buttons) — the dialog is taller than the PineTab screen,
      the QVBoxLayout squeezes the buttons; wrap the content in a QScrollArea +
      compute the min width from the longest label (fontMetrics, +padding).
- [x] **Preferences freezes for seconds on "Set up secure messaging" / "Reset
      cross-signing"** (+ the "Your devices" query blocks on open) — synchronous
      HTTP on the UI thread; run on a ThreadPool with a busy/disabled button.

## Refactors / cleanup

- [x] **Deduplicate base64 implementations** — DONE (Phase 7 slice 1, Aug 4):
      cross_sign.cpp + backup_crypto.cpp use the canonical string-based base64
      from olm_account via 4-line byte wrappers; the standalone copy is deleted.

## Phase 7 (SSSS + key backup) — remaining slices

- [x] **`/room_keys` API** + store registry + sync integration + UI + live test —
      ALL DONE (Aug 4): create/upload/get/delete, BackupInfo registry,
      dirty-flag auto-upload in the sync loop, PrefsDialog backup section,
      live-Synapse roundtrip green.
- [ ] **Spec-exact session_data KDF for Element interop** — the current
      crypto_box_seal session_data is self-consistent but NOT
      curve25519-aes-sha2-spec (ECDH + AES-256-CBC + HMAC-SHA256 with the spec
      KDF); swap the primitive when Element interop is pursued. The structure
      (version/auth_data/session entries) is already spec-shaped.
- [x] **Secret sharing (SSSS account-data)** — DONE (Aug 4): cross-signing
      private keys encrypted to account-data (m.secret_storage), recovery-key
      unlock, default_key discovery, PrefsDialog sync/retrieve, live-Synapse
      roundtrip green. NOTE: the mm test's `!a2SskSig` assertion stays (device2
      never runs the retrieve flow in CI); the assertion must be re-evaluated
      when the app's cross-device retrieve path is manually verified.

## Testing / validation

- [ ] **Element interop test** — the SAS MSK mac extension + cross-signing +
      backup are self-consistent (two-manager + live tests) but never validated
      against a real Element client. The standing biggest external risk.
- [ ] **App-glue not CI-driven** — the sync-engine Done-handler cross-signing,
      resetCrossSigning, and (once built) the backup flows are replicated in CI,
      not driven through the real app paths.

## Known / accepted (no action)

- RoomMembersDialog batch /keys/query may exceed the server's ~100-user limit
  for very large rooms — degrades gracefully to red shields.
- SAS verification events are plain (unencrypted) to-device per spec; a client
  that Olm-wrapped them would be dropped (we never do).

## Aug 4 evening batch (all landed + CI green)
- [x] Reply-from-Element: sync path now extracts m.in_reply_to + strips the
      fallback quote (sync_applier fastEventToDisplayed)
- [x] m.file/m.audio mxcUrl in all three paths (sync, load-more, history)
- [x] formatted_body nested-object case fixed (structural detection + inner
      escape-aware extraction); top-level "body" no longer grabs the nested one
- [x] Avatar accumulation: room_store keeps the member-avatar map across syncs
      (Synapse omits the state block on incremental syncs); state-only syncs
      still refresh avatars
- [x] Reactions never count as thread replies; encrypted reactions extracted
      as reactions (not rows)
- [x] In-app diagnostics: Log viewer dialog (all channels + filter, menu item
      next to Network log), decrypt-reason badge + tooltip + "Why is this
      encrypted?" context entry, members-load failure reason, own-device
      cross-signing status row, downloadMedia + send + appendBack logs
- [x] Backup/SSSS buttons async (no more UI freeze); loadDevices error path
      restored; onRoomMembers no-room guard
- [x] pendingFetches use-after-free fixed (shared ownership)
- [x] Sync poll default 3000 -> 20000ms (the ~3s delivery delay was the
      long-poll timeout)
- [x] Tests: formatted_body (plain + nested), sync-path reply, m.file/m.audio,
      member-avatar extraction, reaction-count guard, reset re-signs the device

## Aug 5 batch (members fallback + structural dedup + audit rounds)
- [x] Members dialog: local-state fallback when /members fails ("Not in the
      room" on a stale room id) + Reload button + getRoomMembers diagnostic log
- [x] Structural dedup: load-more + history now route through
      fastEventToDisplayed (single extraction path) — they gain decrypt,
      badge/decryptError, reply strip, reactions-as-rows fixes for free
- [x] Load-more: member/system events become system rows, reactions extracted,
      encrypted events decrypted (were blank rows)
- [x] Key-request retry: backoff (30s/2min/10min/1h) + fresh request_id per
      attempt + satisfied-request removal + once-per-sync worker hook + tests
- [x] Own-device status row now truthful (trust passed through, not dead loop)
- [x] Avatar map cleared on room switch (no cross-room contamination)
- [x] Avatar preserved when a re-decrypted event replaces its row
- [x] Double-unescape removed (extractor unescapes; callers no longer re-wrap)
- [x] Tooltip wording softened ("a key request is sent…")

## Aug 5 batch (key-request UX + room-key chat events)
- [x] Key requests: auto-retry now runs on EVERY sync tick in the core loop
      (was: only on data syncs — retries stalled on quiet rooms)
- [x] Manual "Ask for keys" (Element parity): context menu action on
      undecryptable messages -> reRequestKey (fresh request_id, bypasses dedup)
- [x] Chat rows: "X sent us the room key (session …)" (Received),
      "No key yet — requested from X (session …)" (+ "again" on retries),
      "You shared the room key" (once per session, send + thread paths)
- [x] Shared buildRoomKeyRequestJson helper (was hand-built 3x)
- [x] handleRoomKey/handleForwardedRoomKey carry the sender for the rows
- [x] Tests: Received + forced re-request notifications in the mm scenario
- [x] Media + reactions now encrypt in encrypted rooms (755d9a1)
- [ ] Follow-up: receive-side m.encrypted media (file:) support + AES media
      encryption (Element sends/expects encrypted media in E2EE rooms)
- [ ] Follow-up: /me emote checks room state (DEBT(E2EE) chat_view.cpp:47)

## Aug 5 batch 2 (encrypted media + key-request parity + cross-client SAS + UX)
- [x] Encrypted media both ways (m.encrypted v2): media_crypto (AES-256-CTR +
      sha256-of-plaintext), downloadMediaEncrypted, file:/info.thumbnail_file
      extraction in fastEventToDisplayed, ImageLoader encrypted fetch,
      attachment open/save decrypts, sendFile uploads ciphertext + emits
      file:{...} with mimetype/filename, echo carries the keys, upload-failure
      notices
- [x] "Download…" context action on media rows (save-as, encrypted-aware)
- [x] Key requests (Element parity): persisted in SessionStore, sent to ALL
      of the sender's devices (keys/query; wildcard fallback), cancelled
      (request_cancellation) when the key arrives, re-asked after SAS Done
- [x] Cross-client SAS: Olm-wrapped verification events dispatched (incoming)
      + outgoing verification sends Olm-wrapped with plain fallback
- [x] Retry rows: drained on EVERY sync (quiet rooms), capped 3/sync + summary
- [x] Stale member cache: applier extracts displayname; handler cache merged
      from sync state on every sync
- [x] Profile dialog: Verify… button (forwards to VerificationHandler) +
      Make admin (100) — promote was only 50 (moderator)
- [x] Open room's encryption flag refreshed on sync (encrypted_ follows
      m.room.encryption changes mid-session)
- [x] Log viewer: refresh() channel-mapping bug fixed + usage hint + Copy
      button (eee83c4)
- [x] Emote/reaction encryption, share gating + timing logs (eee83c4)
- [x] Forwarded-key request satisfaction + wildcard fallback (eee83c4)
- [x] Tests: test_media_crypto (new), applier file: extraction, live mm
      forwarded-erase + encrypted-blob round trip
- [ ] Follow-up: room/DM creation UI with encryption (app can't create rooms)
- [ ] Follow-up: receive-side GIF (fetchMovie) has no encrypted path
