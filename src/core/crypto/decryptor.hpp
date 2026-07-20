// src/core/crypto/decryptor.hpp — Coordinates decryption of m.room.encrypted events.
//
// Given a m.room.encrypted event from a /sync timeline, this:
//   1. Parses algorithm, sender_key, session_id, ciphertext from the event content
//   2. Looks up the Megolm inbound session
//   3. Decrypts the ciphertext
//   4. Returns the decrypted event JSON (m.room.message etc.)
//
// To-device m.room_key events add new Megolm inbound sessions.
// To-device m.room.encrypted events (Olm 1:1) are handled via OlmSession.
#pragma once

#include "olm_account.hpp"
#include "megolm_store.hpp"
#include <memory>
#include <string>

namespace progressive::desktop {

struct DecryptionResult {
    bool ok = false;
    std::string plaintext;     // decrypted event JSON
    std::string error;
};

class Decryptor {
public:
    Decryptor();
    ~Decryptor();

    // Initialize the Olm account — call once at startup. Creates a new account
    // if no pickle was saved, otherwise loads from the pickle.
    // pickleKey: secret key for olm pickle encryption (use device_id + user_id).
    bool init(const std::string& accountPickle, const std::string& pickleKey);
    bool init();
    bool isInitialized() const { return account_ && account_->isValid(); }

    // Get the OlmAccountStore (for uploading device keys).
    OlmAccountStore* account() { return account_.get(); }
    MegolmStore* megolm() { return megolm_.get(); }

    // Save current olm account pickle for persistence.
    std::string saveAccountPickle(const std::string& pickleKey);

    // Get identity keys (Curve25519 + Ed25519).
    OlmIdentityKeys identityKeys() const;

    // Generate one-time keys for upload. Returns JSON to send to /keys/upload.
    std::string generateOneTimeKeys(int count);

    // Decrypt a m.room.encrypted event (Megolm algorithm).
    DecryptionResult decryptMegolmEvent(const std::string& roomId,
                                          const std::string& senderId,
                                          const std::string& contentJson);

    // Handle a to-device m.room_key event — add the megolm inbound session.
    // contentJson: the content of the to_device event.
    // senderKey: the sender's curve25519 key (from the Olm-encrypted wrapper, if available)
    // For now, senderKey comes from the room_key content itself (some clients include it).
    bool handleRoomKey(const std::string& contentJson);

private:
    std::unique_ptr<OlmAccountStore> account_;
    std::unique_ptr<MegolmStore> megolm_;
};

} // namespace progressive::desktop
