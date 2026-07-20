// src/core/crypto/megolm_store.hpp — Inbound Megolm session storage.
//
// Wraps progressive::MegolmSessionManager with:
//   - O(1) lookup by (roomId, senderKey, sessionId)
//   - Persistence of pickled sessions to SQLite
//   - Pending event queue (events we couldn't decrypt yet, awaiting room_key)
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace progressive::desktop {

struct PendingEncryptedEvent {
    std::string roomId;
    std::string senderKey;
    std::string sessionId;
    std::string ciphertext;      // megolm ciphertext (base64)
    std::string senderId;        // sender user ID
    int64_t originServerTs = 0;
    std::string eventId;
};

class MegolmStore {
public:
    MegolmStore();
    ~MegolmStore();

    // Add a room key received via to-device m.room_key event.
    // sessionKeyBase64: the "session_key" field (base64-encoded Megolm key)
    // Returns true on success.
    bool addInboundSession(const std::string& roomId,
                            const std::string& senderKey,
                            const std::string& sessionId,
                            const std::string& sessionKeyBase64);

    // Try to decrypt a megolm ciphertext.
    // Returns decrypted plaintext or empty string on failure (no session / bad mac).
    std::string decrypt(const std::string& roomId,
                         const std::string& senderKey,
                         const std::string& sessionId,
                         const std::string& ciphertext);

    // Check if we have a session for this (room, sender, sessionId).
    bool hasSession(const std::string& roomId,
                     const std::string& senderKey,
                     const std::string& sessionId);

    // Add an event to the pending queue (couldn't decrypt yet).
    void addPending(const PendingEncryptedEvent& evt);

    // Get all pending events matching a newly-added session, then remove them.
    std::vector<PendingEncryptedEvent> takePendingForSession(
        const std::string& roomId,
        const std::string& senderKey,
        const std::string& sessionId);

    // Pickle all sessions to a string for persistence (key = pickle key).
    std::string pickleAll(const std::string& key);

    // Unpickle sessions from a string.
    bool unpickleAll(const std::string& key, const std::string& data);

    // Stats
    int sessionCount();
    int pendingCount();

private:
    // Use progressive::MegolmSessionManager — but we need to call into progressive_native.
    // We forward to it via pimpl to keep includes out of the header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mtx_;
    std::vector<PendingEncryptedEvent> pending_;
};

} // namespace progressive::desktop
