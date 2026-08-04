# Checklist — pending / remembered items

Tracked so nothing found in the audits gets forgotten. Items are small or deferred;
completed items are removed.

## Refactors / cleanup

- [ ] **Deduplicate base64 implementations** — `backup_crypto.cpp` has its own
      b64Encode/b64Decode, a third copy alongside `cross_sign.cpp`'s (and the
      submodule's). Create a shared `src/core/crypto/base64.hpp` util and use it
      in both. Not urgent (all copies are correct); found Aug 4, Phase 7 slice 1.

## Phase 7 (SSSS + key backup) — remaining slices

- [ ] **`/room_keys` API** — `MatrixClient`: POST /room_keys/version (create,
      auth_data.public_key), PUT /room_keys/keys (all sessions, batched),
      GET /room_keys/version/{v} + GET /room_keys/keys, DELETE /room_keys/version/{v}
- [ ] **Real-export roundtrip test** — real megolm export -> backup encrypt ->
      restore decrypt -> `addImportedSession` -> decrypt an old message
      (pending-replay path)
- [ ] **Sync integration** — backup creation/upload from the sync thread; a
      backup-version registry in the store (version id + key material,
      re-encrypt on megolm rotation)
- [ ] **UI** — PrefsDialog "Key backup": Create (show the recovery key ONCE +
      "I wrote it down" confirmation), Backup now, Restore (paste key -> fetch ->
      import), status label
- [ ] **Spec-exact session_data KDF for Element interop** — the current
      crypto_box_seal session_data is self-consistent but NOT
      curve25519-aes-sha2-spec (ECDH + AES-256-CBC + HMAC-SHA256 with the spec
      KDF); swap the primitive when Element interop is pursued. The structure
      (version/auth_data/session entries) is already spec-shaped.
- [ ] **Secret sharing (SSSS account-data)** — after the backup slice: devices
      can decrypt the backup private key from account-data -> device2 gets the
      SSK -> the mm test's `!a2SskSig` assertion must be REMOVED then.
- [ ] **Roadmap update** — docs/E2EE-roadmap.md has no Phase 7 section yet;
      slice 1 (base58, recovery key, backup crypto, roundtrip test, the 2
      libsodium AArch64 quirks) is undocumented there.

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
