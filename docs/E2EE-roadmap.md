# E2EE Full Implementation Roadmap

> 7 phases from alpha E2EE to full spec compliance.
> Each phase = 1-5 AI coder sessions. Total: ~4-6 weeks.
> Last updated: July 30, 2026

## Current state (after alpha hardening)
- Olm 1:1 sessions (create, persist, decrypt) ✓
- Megolm inbound + outbound (encrypt, decrypt, share room_key) ✓
- OTK management (upload, count, smart generation) ✓
- Device keys (upload, sign, device_lists tracking) ✓
- Recovery chain (m.dummy + m.room_key_request outgoing) ✓
- Multi-account E2EE (shared flag, per-account scoping) ✓
- Outbound Megolm persistence ✓
- Ed25519 signature verification ✓ (Phase 1)
- Olm plaintext validation ✓ (Phase 1)
- OTK + device key signature verification ✓ (Phase 1)

## Phase 1 — Signature verification foundation ✅ COMPLETE
The linchpin. Without ed25519 verify, no signature can be checked.
- Ed25519 verify primitive: wrap `olm_ed25519_verify` (PUBLIC API, olm.h:516) in `ed25519Verify(key, message, signature) → bool`. Replaces submodule stub.
- Olm plaintext validation: verify `sender`, `recipient`, `recipient_keys`, `keys` fields per m.olm.v1 spec.
- OTK + device key signature verification: verify signed_curve25519 + device_keys signatures.
- Tests: sign+verify roundtrip, tampered sig rejected.

Unlocks: cross-signing (Phase 6), SAS MAC verify (Phase 2).

## Phase 2 — SAS verification (device verification) 🔄 IN PROGRESS
The user-visible "make the red shield go away."
- Port sas_verification.cpp (OlmSAS wrapper — REAL in submodule, 212L)
- Port verification_utils.cpp (64-emoji table, decimal computation, message builders — REAL, fix 1 bug)
- Implement m.key.verification.* state machine (request→ready→start→accept→key→mac→done/cancel)
- To-device + in-room routing for 8 verification event types
- Qt SAS dialog: 7 emoji display, "They match"/"They don't match" buttons, cancel/timeout
- Self-verification (Settings → "Verify this device")
- User verification (User profile → "Verify")

Status: 6 commits pushed, 12 bugs found in self-review, fix prompt written (not yet applied).

## Phase 3 — Fallback keys (1-2 sessions)
Unblocks P4. Removes "OTKs exhausted → can't receive" failure mode.
- Port generateFallbackKey from olm_session.cpp's OlmAccountData API to the OlmAccount class
- Wire device_unused_fallback_key_types from /sync → fallback generation trigger
- Upload fallback_key on /keys/upload
- Mark fallback as published after successful claim

NOT blocked on submodule anymore — real implementation exists in OlmAccountData API.

## Phase 4 — Key sharing + forwarded keys + export/import (2-3 sessions)
- Port keyshare.cpp (incoming m.room_key_request handling — REAL, 103L)
  - shouldShareKey policy
  - buildForwardedKeyContent (with MSC3061 shared_history)
- Wire to-device handler for incoming m.room_key_request
- m.forwarded_room_key receive + import via olm_import_inbound_group_session
- Key export/import file format (MegolmSessionData JSON envelope)
- Key export/import UI

## Phase 5 — Megolm rotation (1 session)
- Port room_encryption.cpp isEncryptionRotationDue() (REAL, 123L)
- Wire into outbound session path: when due, drop + recreate outbound
- Forward secrecy: don't re-share rotated keys to late joiners
- Parse rotation config from m.room.encryption state event

## Phase 6 — Cross-signing (3-4 sessions)
Depends on Phase 1 (ed25519 verify).
- Port cross_signing_manager.cpp data model (PARTIAL — trust checks exist)
- Implement MSK/USK/SSK key generation (ed25519 keypairs)
- Sign SSK+USK with MSK; upload to account_data
- Sign own device with SSK; sign other user's MSK with USK
- Trust computation with REAL signature verification (Phase 1 primitive)
- UI: "Set up secure messaging" flow, device shields (red/grey/green), cross-signing reset

## Phase 7 — SSSS + key backup (4-5 sessions)
The hardest phase — crypto core must be written from scratch.
- Port key_backup.cpp recovery key format (REAL — base58, parity, curve-key)
- Implement Megolm backup encryption: Curve25519 ECDH + AES-256-CBC + HMAC-SHA-256
- Backup version create/query/delete (POST/GET/DELETE /room_keys/*)
- Upload + download + decrypt session keys
- SSSS secret storage: store/read MSK private key encrypted
- UI: key backup setup (recovery key display, passphrase entry), backup restore on login

## Submodule FAKE-boilerplate trap warning
DO NOT PORT these files — they are auto-generated JSON-echo no-ops:
- All *_v4.cpp files (~220KB total): verification_v4, cross_sign_v4, key_backup_v4, secret_store_v4, etc.
- crypto_ops.cpp (34KB)
- sas_manager.cpp, backup_controller.cpp, gossip_manager.cpp
- Most *_utils.cpp, *_manager.cpp files (with exceptions noted above)
- dehydrate_utils.cpp, key_share.cpp (the fake one), key_share_handler.cpp

## REAL submodule files safe to port
- sas_verification.cpp (212L) — OlmSAS wrapper (13 olm_sas_* calls)
- verification_utils.cpp (156L) — 64-emoji table + builders (1 known bug)
- keyshare.cpp (103L) — incoming key request policy + builders
- room_encryption.cpp (123L) — rotation logic
- key_backup.cpp (329L) — recovery key format only (not backup crypto)
- cross_signing_manager.cpp (342L) — data model only (not crypto)
- crypto_algorithms.cpp — SHA/HMAC/HKDF primitives
- olm_session.cpp:157 — fallback key (on OlmAccountData API)
- megolm_decryptor.cpp — inbound Megolm export

## What's missing (must write from scratch)
- Ed25519 verify primitive → ✅ DONE (Phase 1)
- Full SAS state machine → 🔄 IN PROGRESS (Phase 2)
- Cross-signing keygen + signing → Phase 6
- SSSS key backup encryption → Phase 7 (hardest — no submodule code)
- QR verification → not planned
- Device dehydration → not planned

## Spec references
- Matrix spec: "End-to-End Encryption", "Key management", "Server-side key backups", "Secret Storage", "Cross-signing", "Key verification", "To-device messaging"
- MSC1756 (cross-signing), MSC1543 (QR verification), MSC3061 (shared history)
- MSC3976 (fallback key usage), MSC3847 (device dehydration)
- MSC3894 (secret gossiping), MSC3086 (SAS emoji set)
