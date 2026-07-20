// src/core/crypto/olm_account.hpp — OlmAccount lifecycle + device keys.
//
// Wraps progressive::OlmAccount (libolm) for desktop use:
//   - Create/load account, generate identity keys + one-time keys
//   - Upload device keys via /keys/upload
//   - Sign JSON for device verification
//   - Persist account pickle in SQLite
#pragma once

#include <string>
#include <optional>

namespace progressive::desktop {

struct OlmIdentityKeys {
    std::string curve25519;  // identity key for 1:1 Olm sessions
    std::string ed25519;     // fingerprint key for signing
};

class OlmAccountStore {
public:
    OlmAccountStore();
    ~OlmAccountStore();

    // Create a new account (call once on first login).
    // Returns false if account already exists or creation failed.
    bool create();

    // Load account from pickle string. Returns false on failure.
    bool load(const std::string& pickle, const std::string& key);

    // Save account to pickle string. Returns empty on failure.
    std::string save(const std::string& key);

    // Get identity keys (Curve25519 + Ed25519).
    OlmIdentityKeys identityKeys() const;

    // Get Curve25519 key only.
    std::string curve25519Key() const;

    // Get Ed25519 key only.
    std::string ed25519Key() const;

    // Sign a message with Ed25519 key. Returns base64 signature.
    std::string sign(const std::string& message) const;

    // Generate one-time keys (call before uploading to server).
    // Returns the JSON of one-time keys to upload.
    std::string generateOneTimeKeys(int count);

    // Mark current one-time keys as published (called after /keys/upload success).
    void markOneTimeKeysPublished();

    bool isValid() const { return account_ != nullptr; }

    // Access the underlying progressive::OlmAccount for OlmSession operations.
    void* rawAccount() { return account_; }

private:
    void* account_ = nullptr;  // progressive::OlmAccount*
    friend class Decryptor;
};

// Base64 encode/decode for olm pickles (URL-safe-ish, like libolm uses)
std::string base64Encode(const std::string& data);
std::string base64Decode(const std::string& b64);

} // namespace progressive::desktop
