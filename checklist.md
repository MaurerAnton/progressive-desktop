# Checklist — pending / remembered items

Tracked so nothing found in the audits gets forgotten. Items are small or deferred;
completed items are removed.

## Refactors / cleanup

- [ ] **Deduplicate base64 implementations** — `backup_crypto.cpp` has its own
      b64Encode/b64Decode, a third copy alongside `cross_sign.cpp`'s (and the
      submodule's). Create a shared `src/core/crypto/base64.hpp` util and use it
      in both. Not urgent (all copies are correct); found Aug 4, Phase 7 slice 1.

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
