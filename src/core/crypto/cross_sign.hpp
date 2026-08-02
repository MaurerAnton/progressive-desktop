// src/core/crypto/cross_sign.hpp — cross-signing keys (MSC1756) via libsodium.
#pragma once
#include <string>

namespace progressive::desktop {

struct CrossSigningKeys {
    std::string masterPub, masterPriv;
    std::string userPub, userPriv;
    std::string selfPub, selfPriv;
};

// Generate MSK/USK/SSK ed25519 keypairs (libsodium crypto_sign_keypair).
// Keys are base64-encoded (64-byte libsodium secret keys, 32-byte publics).
CrossSigningKeys generateCrossSigningKeys();

// Sign a message with a base64 ed25519 private key -> base64 signature.
std::string signEd25519(const std::string& privKeyB64, const std::string& message);

// Verify a base64 ed25519 signature over a message.
bool verifyEd25519(const std::string& pubKeyB64, const std::string& message,
                   const std::string& sigB64);

// Build the m.cross_signing.{master,self_signing,user_signing} account_data
// content JSON. signingKeyB64/privKeyB64: the key that signs this content
// (master for self/user_signing; empty for master itself).
// userId: our user id (used in the signatures map).
std::string buildCrossSigningContent(const std::string& type,
                                     const std::string& pubKeyB64,
                                     const std::string& signingPubB64,
                                     const std::string& signingPrivB64,
                                     const std::string& userId);

// Derive the public key from a base64 libsodium secret key (last 32 bytes).
std::string crossSigningPubFromPriv(const std::string& privKeyB64);

// Canonical form of the "keys" object used as the signature message.
std::string crossSigningKeysCanonical(const std::string& pubKeyB64);

} // namespace progressive::desktop
