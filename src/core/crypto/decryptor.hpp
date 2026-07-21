// src/core/crypto/decryptor.hpp — Coordinates E2EE: Olm + Megolm.
//
// Provides:
//   - OlmAccount setup + device key signing for /keys/upload
//   - Inbound Olm 1:1 session management (receive m.room.encrypted to-device)
//   - Inbound Megolm session management (decrypt room timeline messages)
//   - Outbound Megolm session per encrypted room (encrypt outgoing messages)
//   - Room key sharing via Olm 1:1 (sends m.room_key to all devices)
#pragma once

#include "olm_account.hpp"
#include "megolm_store.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace progressive::desktop {

struct DecryptionResult {
    bool ok = false;
    std::string plaintext;     // decrypted event JSON
    std::string error;
};

// Per-room outbound megolm session. Created when we first send a message
// to an encrypted room. The session key is shared with all room members
// via m.room_key to-device events (Olm 1:1 encrypted).
struct OutboundMegolmSession {
    std::string sessionId;
    std::string sessionKey;     // base64 — for sharing with other devices
    void* session = nullptr;     // OlmOutboundGroupSession* (libolm)
    int messageIndex = 0;
};

class Decryptor {
public:
    Decryptor();
    ~Decryptor();

    // ---- Account lifecycle ----
    bool init(const std::string& accountPickle, const std::string& pickleKey);
    bool init();
    bool isInitialized() const { return account_ && account_->isValid(); }

    // Save/load the olm account pickle for persistence.
    std::string saveAccountPickle(const std::string& pickleKey);

    OlmIdentityKeys identityKeys() const;
    std::string curve25519Key() const;
    std::string ed25519Key() const;

    // ---- Device key upload ----
    // Builds the /keys/upload body JSON with signed device_keys + one-time keys.
    // userId + deviceId identify whose keys these are.
    // Generates `count` one-time keys before building the body.
    std::string buildKeysUploadBody(const std::string& userId,
                                      const std::string& deviceId,
                                      int oneTimeKeyCount = 10);

    // ---- Inbound Megolm (room message decryption) ----
    // Decrypts a m.room.encrypted event (Megolm algorithm).
    DecryptionResult decryptMegolmEvent(const std::string& roomId,
                                          const std::string& senderId,
                                          const std::string& contentJson);

    // Handle a to-device m.room_key event — adds the megolm inbound session.
    bool handleRoomKey(const std::string& contentJson);

    // ---- Inbound Olm 1:1 (to-device decryption) ----
    // Handle a to-device m.room.encrypted event (Olm 1:1 algorithm).
    // Decrypts the ciphertext using an inbound OlmSession, then if the
    // inner plaintext is a m.room_key, calls handleRoomKey.
    // Returns the inner plaintext (for logging/processing) or empty on failure.
    std::string handleOlmEncryptedToDevice(const std::string& senderId,
                                              const std::string& contentJson);

    // ---- Outbound Megolm (room message encryption) ----
    // Get or create an outbound megolm session for a room.
    // Returns the session ID (used in the m.room.encrypted event).
    // Caller must hold the session mutex while encrypting.
    std::string getOrCreateOutboundSession(const std::string& roomId);

    // Encrypt a plaintext message event JSON for a room.
    // Returns the content JSON for m.room.encrypted:
    //   {"algorithm":"m.megolm.v1.aes-sha2","ciphertext":"...","sender_key":"...",
    //    "device_id":"...","session_id":"..."}
    std::string encryptMessage(const std::string& roomId,
                                 const std::string& deviceId,
                                 const std::string& plaintextEventJson);

    // Get the session key for sharing (for m.room_key to-device events).
    // Returns empty if no outbound session exists for the room.
    std::string getOutboundSessionKey(const std::string& roomId);

    // Get the outbound session ID for a room.
    std::string getOutboundSessionId(const std::string& roomId);

    // Check if we have an outbound session for this room.
    bool hasOutboundSession(const std::string& roomId);

    // Drop the outbound session for a room (e.g. when room is left).
    void dropOutboundSession(const std::string& roomId);

    // ---- Room key sharing (full E2EE outbound) ----
    // Shares the outbound megolm session key with all room members' devices.
    // Steps:
    //   1. Query device keys for the given user IDs via /keys/query
    //   2. Claim one-time keys for each device via /keys/claim
    //   3. For each device: create OlmSession outbound, encrypt m.room_key
    //   4. Send m.room.encrypted to-device events via /sendToDevice
    // Returns true on success. Logs progress to stderr.
    // userIds: list of room member user IDs (excluding our own).
    // userId/deviceId: our identity (for excluding self + signing).
    bool shareRoomKey(const std::string& roomId,
                        const std::vector<std::string>& userIds,
                        const std::string& ourUserId,
                        const std::string& ourDeviceId,
                        const std::string& homeserverUrl,
                        const std::string& accessToken);

    // Access internal stores for direct manipulation (e.g. persistence).
    OlmAccountStore* account() { return account_.get(); }
    MegolmStore* megolm() { return megolm_.get(); }

    // Olm 1:1 session persistence.
    std::string pickleOlmSessions(const std::string& key);
    bool unpickleOlmSessions(const std::string& key, const std::string& data);

private:
    std::unique_ptr<OlmAccountStore> account_;
    std::unique_ptr<MegolmStore> megolm_;
    // Per-room outbound megolm sessions.
    std::unordered_map<std::string, OutboundMegolmSession> outboundSessions_;
    std::mutex outboundMtx_;
    // Inbound Olm 1:1 sessions, keyed by (senderCurve25519).
    // We store them as pickled strings; created on-demand from pre-key messages.
    std::unordered_map<std::string, std::string> olmSessions_;  // senderKey → pickle
    std::mutex olmMtx_;

    // Sign a canonical JSON string with Ed25519. Returns base64 signature.
    std::string signCanonicalJson(const std::string& canonicalJson);

    // Try to create an inbound OlmSession from a pre-key message.
    // Stores the session pickle for future use.
    bool createInboundOlmSession(const std::string& preKeyMessage,
                                   const std::string& senderIdentityKey);
};

} // namespace progressive::desktop
