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

## Aug 5 batch 3 (root-cause fixes + audit batch)
- [x] ROOT CAUSE: Olm decrypt failure (BAD_MESSAGE_MAC / no session / pre-key
      failure) now recovers — forceNewOlmSession (m.dummy) was dead code;
      stale pickles dropped so the peer's next pre-key starts clean. This was
      why Element's m.room_key answers never decrypted -> "no key yet" forever
- [x] Crypto state persists every 20 syncs + on logout (was: only on clean
      close — a PineTab power-off lost sessions and deadlocked peers)
- [x] /sync OTK count parsed correctly (device_one_time_keys_count per-device
      + one_time_keys_count fallback) — the "count=0 -> uploading fresh keys"
      spam loop every sync is gone
- [x] Avatar thumbnail 404 fallback (full download) — missing avatars
- [x] Txn-id collisions fixed: all sends use genTxnId (two messages in the
      same second were silently dropped by the server)
- [x] Plain "*" wildcard key requests (Olm-to-* is impossible)
- [x] mimetype from file.mimetype / info.mimetype (encrypted media rows)
- [x] Download… works for empty-body media rows
- [x] Outbound Olm session cache cleared on init/setCryptoContext (identity
      changes)
- [x] Encrypted images prefer the small info.thumbnail_file
- [x] /me failures show an error notice (was a lying echo)
- [x] Log viewer seq-set pruning (memory)
- [x] "Copy text" context action on messages
- [x] Upload filenames percent-encoded (Cyrillic/space/& safe)
- [x] Key-request rate gate (10 per 5s) — history loads no longer flood
- [x] Room-list/notification previews show "[encrypted]" for encrypted rooms
- [x] Tests: genTxnId uniqueness, file.mimetype, [encrypted] preview

## Aug 5 batch 4 (crash fix + recovery completeness + encrypted menu actions)
- [x] CRASH FIX: closeEvent stop() BEFORE persistCrypto() (the same-thread
      double-lock on olmMtx_); persistCrypto serialized (persistMtx_); the
      Olm recovery (forceNewOlmSession, HTTP) moved OUT of the olmMtx_ lock —
      deferred + executed after the lock scope ends
- [x] Recovery completeness: time-bounded m.dummy dedup (10 min, cleared on
      init) — self-heals when the peer's Element rotates; pending key
      requests for that sender re-armed (attempts reset) so they re-fire;
      status-line hint with guidance when recovery triggers
- [x] requestedKeys_ capped at 200 (evict oldest)
- [x] Context-menu reactions now encrypted in E2EE rooms (was plaintext)
- [x] Message edits now encrypted in E2EE rooms (was plaintext leak)
- [x] Incoming m.replace (edits) handled: applier extracts target + new
      content; sync/history/load-more update the original row + "(edited)"
      instead of rendering duplicate rows
- [x] Test: applier m.replace case
- [ ] Follow-up: notification click -> open the room (optional)

## Aug 5 batch 5 (identity reset heals broken Olm chains)
- [x] "Reset device keys" now REGENERATES the identity (OlmAccountStore::reset:
      destroy + rebuild — olm_create_account requires uninitialized memory) and
      clears the whole 1:1 session layer (inbound pickles, outbound cache,
      pending key requests). Keeps inbound megolm sessions (history stays
      decryptable). Peers' stale sessions die with the old identity; they
      re-establish fresh pre-key sessions on next contact — this heals the
      BAD_MESSAGE_MAC deadlock that m.dummy cannot rotate (the peer keeps
      reusing its broken session). Dialog text updated with guidance.
- [ ] Follow-up: multi-account session hygiene — the runtime account can
      differ from the bootstrapped one after an account switch + pre-refresh
      (log: sync errors name @t1s3 while the UI loaded @test_1); verify the
      switcher's active-account persistence

## Aug 5 batch 6 (multi-account root causes + recovery fixes + reset UX)
- [x] ROOT CAUSE (clone/all-A identities): OlmAccountStore::reset() rebuilds the
      wrapper without creating (fresh uninitialized libolm memory); every
      Decryptor::init() resets first — unpickling over an initialized account
      corrupted identities across accounts/switches/logins
- [x] init() clears ALL per-account state: olm pickles, outbound cache+megolm
      sessions+roomKeys, pending key requests, notifications, recovery notes,
      broken-Olm marks, stale users, re-decrypted events
- [x] MegolmStore::unpickleAll clears the manager + pending first (sessions
      bled across accounts)
- [x] Per-user since token: sync_state gains user_id (migrated); save/load/
      clear take the user; test_phase1 covers per-account isolation
- [x] Startup all-A identity auto-heal: corrupt key detected -> log old key +
      resetIdentity + re-upload device keys
- [x] forceNewOlmSession no longer stores its OUTBOUND pickle in the INBOUND
      store (it poisoned every type-1 decrypt -> BAD_MAC forever); type-1
      failure no longer erases the vector; pre-key store capped at 20/sender
- [x] Decrypt-reason enrichment: markOlmBroken + enrichDecryptError -> the
      badge/tooltip explains "the sender's Olm session with you is broken..."
      (only for actually-broken senders, 30-min window)
- [x] Reset device keys: regeneration decoupled from the server delete (404 =
      "homeserver doesn't support it, identity regenerated anyway"); wrong
      password (401/403 M_FORBIDDEN) reported as such; M_UNKNOWN_TOKEN ->
      re-login; completion summary box

## Round: share-on-join, log viewer, media/downloads, threads, image viewer (Aug 6)

- [x] Log viewer root cause: logToRing + snapshotLogRing used two SEPARATE static
      deques (debug_log.hpp) — snapshot always read an empty ring, so the in-app
      viewer could never show lines (present since e74dfae). Merged into one shared
      ring; test_phase1 round-trip test added.
- [x] ImageLoader: negative cache for failed mxcs (1h cooldown, bounded 1024),
      image cache 20 -> 128, in-flight dedup per mxc, failure-context logging
      ("avatar"/"timeline image"/"room avatar"/"movie"). Stops the per-sync
      retry storm of 404'd media.
- [x] Image viewer: maximized Element-style lightbox, wheel + double-click zoom
      (cursor-centered), Save-as with ORIGINAL bytes, temp-file open with real
      name/extension (no more PNG re-encode), full-image cache in
      AttachmentHandler (48 entries) so re-opening never re-downloads.
- [x] Attachment clicks (video/audio/file): real filename from event body +
      extension from mimetype (was timestamp + msgtype guess).
- [x] Threads: "All threads" passed roomId="" -> broken URL (rooms//threads);
      now requires the current room. List shows root previews (sender, body,
      replies, "You", time); double-click opens the thread view via
      ThreadHandler (replaces the raw-JSON placeholder box). "N replies" bubble
      on thread roots already existed (clickable).
- [x] Share-on-join (the new-member-cant-decrypt root cause): we only shared
      room keys on NEW session creation — a member joining later never got the
      current session key (Element shares on membership change). SyncEngine now
      shares the current outbound key to joiners (timeline + state events,
      deduped per session), shares to all members when WE join, and re-shares
      (rate-limited) when device_lists.changed fires.
- [x] OTK claim retry-once on BAD_MESSAGE_MAC in sendOlmToDevice + shareRoomKey
      (stale keys are consumed by claiming, so a retry often reaches a fresh
      one); discriminator log when it stays invalid ("pool holds keys from an
      older identity — peer must rotate keys").
- [x] uploadDeviceKeys: 400 "already exists" -> immediate discard+generate retry
      (fresh account OTK ids colliding with stale server-side OTKs).
- [x] m.room_key.withheld surfaced: parsed (code/reason/from_device), matched
      against pending requests via sender_key+device, rendered as a system row
      "X withheld the room key (<reason>)".
- [x] Identity-change hint: when Olm ciphertext is addressed to a key we no
      longer hold, each key gets an IDENTITY-HINT log explaining the stale
      peer/server cache.
- [x] Reset device keys: password gate hardened — wrong password now ABORTS
      (no silent regeneration; previously regenerated anyway), M_UNKNOWN_TOKEN
      re-logs-in, 404/unverifiable requires an explicit confirm dialog.

## Deferred (recorded, not client-fixable / out of scope)

- Federation caveat: peers on xmr.se (offcrise, @12a19e18_2ea13) keep stale
  device-key/OTK caches of our reset accounts — their Olm to us targets keys we
  no longer hold and their served OTKs can fail verification. Client-side fix:
  NONE (server-side cache/federation); peers must refresh/reset in their client.
- Threads: global "All threads" across ALL rooms (per-room list only now); the
  /threads API is per-room, so a global view needs per-room fan-out.
- Video/audio playback in-app (currently opens the system player).
