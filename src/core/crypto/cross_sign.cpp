// src/core/crypto/cross_sign.cpp — cross-signing key generation + signing.
#include "cross_sign.hpp"

#include <sodium.h>
#include <cstring>
#include <vector>

namespace progressive::desktop {

namespace {

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64Encode(const uint8_t* data, size_t len) {
    std::string r;
    int val = 0, vb = -6;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) + data[i]; vb += 8;
        while (vb >= 0) { r.push_back(kB64[(val >> vb) & 0x3F]); vb -= 6; }
    }
    if (vb > -6) r.push_back(kB64[((val << 8) >> (vb + 8)) & 0x3F]);
    while (r.size() % 4) r.push_back('=');
    return r;
}

std::vector<uint8_t> b64Decode(const std::string& in) {
    std::vector<uint8_t> r;
    int val = 0, vb = -8;
    for (char c : in) {
        if (c == '=') break;
        const char* p = strchr(kB64, c);
        if (!p) continue;
        val = (val << 6) + (int)(p - kB64); vb += 6;
        if (vb >= 0) { r.push_back((uint8_t)((val >> vb) & 0xFF)); vb -= 8; }
    }
    return r;
}

bool sodiumInitialized() {
    static const bool ok = (sodium_init() >= 0);
    return ok;
}

} // namespace

CrossSigningKeys generateCrossSigningKeys() {
    CrossSigningKeys keys;
    if (!sodiumInitialized()) return keys;
    auto gen = [](std::string& pub, std::string& priv) {
        uint8_t p[crypto_sign_PUBLICKEYBYTES];
        uint8_t s[crypto_sign_SECRETKEYBYTES];
        if (crypto_sign_keypair(p, s) != 0) return false;
        pub = b64Encode(p, sizeof(p));
        priv = b64Encode(s, sizeof(s));
        return true;
    };
    if (!gen(keys.masterPub, keys.masterPriv)) return {};
    if (!gen(keys.userPub, keys.userPriv)) return {};
    if (!gen(keys.selfPub, keys.selfPriv)) return {};
    return keys;
}

std::string signEd25519(const std::string& privKeyB64, const std::string& message) {
    if (!sodiumInitialized()) return {};
    auto priv = b64Decode(privKeyB64);
    if (priv.size() != crypto_sign_SECRETKEYBYTES) return {};
    uint8_t sig[crypto_sign_BYTES];
    unsigned long long sigLen = 0;
    if (crypto_sign_detached(sig, &sigLen, (const uint8_t*)message.data(),
                             message.size(), priv.data()) != 0)
        return {};
    return b64Encode(sig, sigLen);
}

bool verifyEd25519(const std::string& pubKeyB64, const std::string& message,
                   const std::string& sigB64) {
    if (!sodiumInitialized()) return false;
    auto pub = b64Decode(pubKeyB64);
    auto sig = b64Decode(sigB64);
    if (pub.size() != crypto_sign_PUBLICKEYBYTES || sig.size() != crypto_sign_BYTES)
        return false;
    return crypto_sign_verify_detached(sig.data(), (const uint8_t*)message.data(),
                                       message.size(), pub.data()) == 0;
}

std::string crossSigningKeysCanonical(const std::string& pubKeyB64) {
    return "{\"ed25519:" + pubKeyB64 + "\":\"" + pubKeyB64 + "\"}";
}

std::string crossSigningKeyCanonical(const std::string& pubKeyB64,
                                     const std::string& usage,
                                     const std::string& userId) {
    return "{\"keys\":" + crossSigningKeysCanonical(pubKeyB64)
        + ",\"usage\":[\"" + usage + "\"],\"user_id\":\"" + userId + "\"}";
}

std::string buildCrossSigningContent(const std::string& type,
                                     const std::string& pubKeyB64,
                                     const std::string& signingPubB64,
                                     const std::string& signingPrivB64,
                                     const std::string& userId) {
    std::string usage;
    if (type.find("self_signing") != std::string::npos) usage = "self_signing";
    else if (type.find("user_signing") != std::string::npos) usage = "user_signing";
    else usage = "master";

    std::string keysJson = crossSigningKeysCanonical(pubKeyB64);
    std::string sig;
    if (!signingPrivB64.empty()) {
        // The server verifies the signature over the FULL canonical
        // CrossSigningKey (keys + usage + user_id), not just the keys object.
        std::string signedJson = crossSigningKeyCanonical(pubKeyB64, usage, userId);
        sig = signEd25519(signingPrivB64, signedJson);
    }

    std::string out = "{\"keys\":" + keysJson;
    if (!sig.empty()) {
        out += ",\"signatures\":{\"" + userId
            + "\":{\"ed25519:" + signingPubB64 + "\":\"" + sig + "\"}}";
    } else {
        out += ",\"signatures\":{}";
    }
    out += ",\"usage\":[\"" + usage + "\"],\"user_id\":\"" + userId + "\"}";
    return out;
}

} // namespace progressive::desktop
