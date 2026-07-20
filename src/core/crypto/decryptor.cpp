// src/core/crypto/decryptor.cpp — Decryption coordinator.

#include "decryptor.hpp"

#include <simdjson.h>
#include <string_view>

namespace progressive::desktop {

namespace {

// Extract a string field from a JSON string_view (handles both "k":"v" and "k": "v").
// Returns empty on not found. Does NOT decode escapes (we expect olm keys
// to be base64, no escapes).
std::string extractStr(std::string_view json, std::string_view key) {
    std::string pat1 = std::string("\"") + std::string(key) + "\":\"";
    auto pos = json.find(pat1);
    if (pos != std::string_view::npos) {
        pos += pat1.size();
        auto end = json.find('"', pos);
        if (end != std::string_view::npos) return std::string(json.substr(pos, end - pos));
    }
    std::string pat2 = std::string("\"") + std::string(key) + "\": \"";
    pos = json.find(pat2);
    if (pos != std::string_view::npos) {
        pos += pat2.size();
        auto end = json.find('"', pos);
        if (end != std::string_view::npos) return std::string(json.substr(pos, end - pos));
    }
    return {};
}

} // namespace

Decryptor::Decryptor()
    : account_(std::make_unique<OlmAccountStore>()),
      megolm_(std::make_unique<MegolmStore>()) {}

Decryptor::~Decryptor() = default;

bool Decryptor::init(const std::string& accountPickle, const std::string& pickleKey) {
    if (!accountPickle.empty()) {
        if (!account_->load(accountPickle, pickleKey)) {
            // Failed to load — fall back to creating new account
            return account_->create();
        }
        return true;
    }
    return account_->create();
}

bool Decryptor::init() {
    return account_->create();
}

std::string Decryptor::saveAccountPickle(const std::string& pickleKey) {
    return account_->save(pickleKey);
}

OlmIdentityKeys Decryptor::identityKeys() const {
    return account_->identityKeys();
}

std::string Decryptor::generateOneTimeKeys(int count) {
    return account_->generateOneTimeKeys(count);
}

DecryptionResult Decryptor::decryptMegolmEvent(const std::string& roomId,
                                                  const std::string& senderId,
                                                  const std::string& contentJson) {
    DecryptionResult r;
    // Parse the m.room.encrypted content:
    // {"algorithm":"m.megolm.v1.aes-sha2","ciphertext":"...","sender_key":"...",
    //  "device_id":"...","session_id":"..."}
    std::string_view cv(contentJson);
    auto algorithm = extractStr(cv, "algorithm");
    if (algorithm != "m.megolm.v1.aes-sha2" && algorithm != "m.megolm.v2.aes-sha2") {
        // Could be m.olm.v1.curve25519-aes-sha2 (1:1) — not handled here
        r.error = "unsupported algorithm: " + algorithm;
        return r;
    }
    auto senderKey = extractStr(cv, "sender_key");
    auto sessionId = extractStr(cv, "session_id");
    auto ciphertext = extractStr(cv, "ciphertext");
    if (senderKey.empty() || sessionId.empty() || ciphertext.empty()) {
        r.error = "missing sender_key/session_id/ciphertext";
        return r;
    }

    if (!megolm_->hasSession(roomId, senderKey, sessionId)) {
        r.error = "no megolm session — waiting for room_key";
        return r;
    }

    auto plaintext = megolm_->decrypt(roomId, senderKey, sessionId, ciphertext);
    if (plaintext.empty()) {
        r.error = "megolmDecrypt failed (bad mac or unknown session)";
        return r;
    }
    r.ok = true;
    r.plaintext = std::move(plaintext);
    return r;
}

bool Decryptor::handleRoomKey(const std::string& contentJson) {
    // m.room_key content: {"algorithm":"m.megolm.v1.aes-sha2",
    //   "room_id":"!...","session_id":"...","session_key":"...","keys":{}}
    std::string_view cv(contentJson);
    auto roomId = extractStr(cv, "room_id");
    auto sessionId = extractStr(cv, "session_id");
    auto sessionKey = extractStr(cv, "session_key");
    auto senderKey = extractStr(cv, "sender_key");
    if (roomId.empty() || sessionId.empty() || sessionKey.empty()) {
        return false;
    }
    // senderKey may be in the room_key content (some clients include it under "keys")
    // or in the outer to-device event's sender_key. Caller should pass via content.
    // We try the content first; if absent, we cannot add the session.
    if (senderKey.empty()) {
        // Try the keys.ed25519 or keys.curve25519 pattern
        auto keys = extractStr(cv, "keys");
        if (!keys.empty()) senderKey = extractStr(keys, "curve25519");
    }
    if (senderKey.empty()) return false;
    return megolm_->addInboundSession(roomId, senderKey, sessionId, sessionKey);
}

} // namespace progressive::desktop
