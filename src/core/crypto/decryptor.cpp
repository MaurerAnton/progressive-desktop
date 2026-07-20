// src/core/crypto/decryptor.cpp — E2EE coordinator (Olm + Megolm).

#include "decryptor.hpp"

#include <progressive/olm.hpp>
#include <olm/olm.h>
#include <olm/outbound_group_session.h>

#include <simdjson.h>
#include <string_view>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sstream>

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

std::string Decryptor::curve25519Key() const {
    return account_->curve25519Key();
}

std::string Decryptor::ed25519Key() const {
    return account_->ed25519Key();
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

// ---- Device key upload body builder ----

std::string Decryptor::signCanonicalJson(const std::string& canonicalJson) {
    return account_->sign(canonicalJson);
}

std::string Decryptor::buildKeysUploadBody(const std::string& userId,
                                              const std::string& deviceId,
                                              int oneTimeKeyCount) {
    // 1. Generate one-time keys
    std::string oneTimeKeysJson = account_->generateOneTimeKeys(oneTimeKeyCount);

    // 2. Build device_keys object with sorted keys (canonical JSON).
    auto keys = account_->identityKeys();
    // The device_keys JSON (without signatures):
    // {"algorithms":[...],"device_id":"...","keys":{...},"user_id":"..."}
    std::ostringstream dk;
    dk << "{\"algorithms\":[\"m.olm.v1.curve25519-aes-sha2\",\"m.megolm.v1.aes-sha2\"],"
       << "\"device_id\":\"" << deviceId << "\","
       << "\"keys\":{"
       << "\"curve25519:" << deviceId << "\":\"" << keys.curve25519 << "\","
       << "\"ed25519:" << deviceId << "\":\"" << keys.ed25519 << "\""
       << "},"
       << "\"user_id\":\"" << userId << "\""
       << "}";
    std::string deviceKeysCanonical = dk.str();

    // 3. Sign the device_keys canonical JSON
    std::string signature = signCanonicalJson(deviceKeysCanonical);

    // 4. Parse the one-time keys JSON and sign each one.
    // The oneTimeKeysJson from progressive::OlmAccount looks like:
    //   {"curve25519:AAAA":"<key>","curve25519:BBBB":"<key>"}
    // We need to:
    //   a) Rename "curve25519:" prefix to "signed_curve25519:"
    //   b) Sign each key object ({"key":"<value>"}) and add signature
    // For simplicity, we use simdjson to parse and re-build the signed format.
    std::ostringstream otkSigned;
    otkSigned << "{";
    bool firstOtk = true;
    simdjson::dom::parser parser;
    auto otkResult = parser.parse(oneTimeKeysJson);
    if (otkResult.error() == simdjson::SUCCESS) {
        auto obj = otkResult.value().get_object();
        if (obj.error() == simdjson::SUCCESS) {
            for (auto field : obj.value()) {
                std::string_view key(field.key);
                auto valStr = field.value.get_string();
                if (valStr.error() != simdjson::SUCCESS) continue;

                // Sign the key object: {"key":"<value>"}
                std::string keyObj = "{\"key\":\"" + std::string(valStr.value()) + "\"}";
                std::string sig = signCanonicalJson(keyObj);

                // Rename curve25519: → signed_curve25519:
                std::string signedKey = "signed_curve25519:" + std::string(key).substr(key.find(':') + 1);
                if (!firstOtk) otkSigned << ",";
                firstOtk = false;
                otkSigned << "\"" << signedKey << "\":"
                          << "{\"key\":\"" << std::string(valStr.value()) << "\","
                          << "\"signatures\":{\""
                          << userId << "\":{\"ed25519:" << deviceId << "\":\"" << sig << "\"}}}";
            }
        }
    }
    otkSigned << "}";

    // 5. Assemble the full /keys/upload body
    std::ostringstream body;
    body << "{\"device_keys\":" << deviceKeysCanonical
         << ",\"signatures\":{\""
         << userId << "\":{\"ed25519:" << deviceId << "\":\"" << signature << "\"}}"
         << ",\"one_time_keys\":" << otkSigned.str()
         << "}";
    return body.str();
}

// ---- Olm 1:1 inbound session management ----

std::string Decryptor::handleOlmEncryptedToDevice(const std::string& senderId,
                                                       const std::string& contentJson) {
    // m.room.encrypted to-device content (Olm 1:1):
    //   {"algorithm":"m.olm.v1.curve25519-aes-sha2","ciphertext":
    //    {"<our_curve25519>":{"body":"<base64>","type":0}},"sender_key":"<their_curve25519>"}
    std::string_view cv(contentJson);
    auto algorithm = extractStr(cv, "algorithm");
    if (algorithm != "m.olm.v1.curve25519-aes-sha2") return {};

    auto senderKey = extractStr(cv, "sender_key");
    if (senderKey.empty()) return {};

    // Find the ciphertext entry for our curve25519 key
    std::string ourCurve = account_->curve25519Key();
    if (ourCurve.empty()) return {};

    // The ciphertext is an object keyed by our curve25519. Find it.
    std::string needle = "\"" + ourCurve + "\":";
    auto pos = contentJson.find(needle);
    if (pos == std::string::npos) return {};
    // Skip to the opening brace of the inner object
    pos = contentJson.find('{', pos);
    if (pos == std::string::npos) return {};
    // Find matching close brace
    int depth = 0;
    size_t endPos = pos;
    for (; endPos < contentJson.size(); ++endPos) {
        if (contentJson[endPos] == '{') depth++;
        else if (contentJson[endPos] == '}') { depth--; if (depth == 0) { endPos++; break; } }
    }
    std::string cipherObj = contentJson.substr(pos, endPos - pos);
    auto body = extractStr(cipherObj, "body");
    auto typeStr = extractStr(cipherObj, "type");
    int msgType = typeStr.empty() ? 0 : std::stoi(typeStr);

    if (body.empty()) return {};

    // Try to find an existing OlmSession for this sender.
    // If none, create one from the pre-key message (type 0).
    std::lock_guard<std::mutex> lk(olmMtx_);
    std::string plaintext;

    progressive::OlmSession session;
    auto* underlyingAccount = static_cast<progressive::OlmAccount*>(account_->rawAccount());

    if (msgType == 0) {
        // Pre-key message — create inbound session, then decrypt
        auto result = session.createInbound(*underlyingAccount, body);
        if (!result.success) {
            std::fprintf(stderr, "[e2ee] createInbound Olm session failed\n");
            return {};
        }
        // After createInbound, decrypt the message body
        auto decResult = session.decrypt(body, 1);
        if (!decResult.success) {
            std::fprintf(stderr, "[e2ee] Olm decrypt after createInbound failed\n");
            return {};
        }
        plaintext = decResult.data;
    } else {
        // Regular message — use existing session
        std::fprintf(stderr, "[e2ee] Olm 1:1 regular message from %s — not yet "
                             "implemented (needs session pickle management)\n", senderId.c_str());
        return {};
    }

    // If we got plaintext, it's a JSON object like:
    //   {"type":"m.room_key","content":{...}}
    // If type == "m.room_key", call handleRoomKey.
    if (!plaintext.empty()) {
        std::string_view pv(plaintext);
        auto innerType = extractStr(pv, "type");
        if (innerType == "m.room_key") {
            auto innerContentStart = plaintext.find("\"content\":");
            if (innerContentStart != std::string::npos) {
                auto braceStart = plaintext.find('{', innerContentStart);
                int d = 0;
                size_t braceEnd = braceStart;
                for (; braceEnd < plaintext.size(); ++braceEnd) {
                    if (plaintext[braceEnd] == '{') d++;
                    else if (plaintext[braceEnd] == '}') { d--; if (d == 0) { braceEnd++; break; } }
                }
                std::string innerContent = plaintext.substr(braceStart, braceEnd - braceStart);
                // Add the senderKey from the outer event for the megolm session
                // The room_key content may or may not have sender_key. We inject it.
                if (innerContent.find("\"sender_key\"") == std::string::npos) {
                    // Insert before closing }
                    innerContent.insert(innerContent.size() - 1,
                        ",\"sender_key\":\"" + senderKey + "\"");
                }
                handleRoomKey(innerContent);
            }
        }
        return plaintext;
    }
    return {};
}

// ---- Outbound Megolm sessions ----

std::string Decryptor::getOrCreateOutboundSession(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    auto it = outboundSessions_.find(roomId);
    if (it != outboundSessions_.end()) {
        return it->second.sessionId;
    }

    // Create new outbound megolm session using libolm directly
    size_t sessionSize = olm_outbound_group_session_size();
    void* session = malloc(sessionSize);
    if (!session) return {};
    auto* olmSession = olm_outbound_group_session(session);
    size_t randLen = olm_init_outbound_group_session_random_length(olmSession);
    std::vector<uint8_t> random(randLen);
    for (auto& b : random) b = static_cast<uint8_t>(rand() % 256);
    size_t ret = olm_init_outbound_group_session(olmSession, random.data(), random.size());
    if (ret == olm_error()) {
        free(session);
        return {};
    }

    // Get session ID
    size_t idLen = olm_outbound_group_session_id_length(olmSession);
    std::vector<uint8_t> idBuf(idLen);
    ret = olm_outbound_group_session_id(olmSession, idBuf.data(), idLen);
    if (ret == olm_error()) { free(session); return {}; }
    std::string sessionId(idBuf.begin(), idBuf.end());

    // Get session key (for sharing with other devices)
    size_t keyLen = olm_outbound_group_session_key_length(olmSession);
    std::vector<uint8_t> keyBuf(keyLen);
    ret = olm_outbound_group_session_key(olmSession, keyBuf.data(), keyLen);
    if (ret == olm_error()) { free(session); return {}; }
    std::string sessionKey(keyBuf.begin(), keyBuf.end());

    OutboundMegolmSession s;
    s.session = session;
    s.sessionId = sessionId;
    s.sessionKey = sessionKey;
    s.messageIndex = 0;
    outboundSessions_[roomId] = std::move(s);
    return sessionId;
}

std::string Decryptor::encryptMessage(const std::string& roomId,
                                        const std::string& deviceId,
                                        const std::string& plaintextEventJson) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    auto it = outboundSessions_.find(roomId);
    if (it == outboundSessions_.end()) {
        return {};  // no session — caller should call getOrCreateOutboundSession first
    }

    auto* olmSession = olm_outbound_group_session(it->second.session);
    // libolm overwrites the message buffer — copy plaintext
    size_t ciphertextLen = olm_group_encrypt_message_length(olmSession, plaintextEventJson.size());
    std::vector<uint8_t> ciphertext(ciphertextLen);
    size_t ret = olm_group_encrypt(olmSession,
        reinterpret_cast<uint8_t*>(const_cast<char*>(plaintextEventJson.data())),
        plaintextEventJson.size(),
        ciphertext.data(), ciphertextLen);
    if (ret == olm_error()) return {};

    // Build m.room.encrypted content
    std::string ciphertextB64 = base64Encode(
        std::string(ciphertext.begin(), ciphertext.begin() + ret));
    auto senderKey = account_->curve25519Key();

    std::ostringstream out;
    out << "{\"algorithm\":\"m.megolm.v1.aes-sha2\""
        << ",\"ciphertext\":\"" << ciphertextB64 << "\""
        << ",\"sender_key\":\"" << senderKey << "\""
        << ",\"device_id\":\"" << deviceId << "\""
        << ",\"session_id\":\"" << it->second.sessionId << "\""
        << "}";
    return out.str();
}

std::string Decryptor::getOutboundSessionKey(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    auto it = outboundSessions_.find(roomId);
    if (it == outboundSessions_.end()) return {};
    return it->second.sessionKey;
}

std::string Decryptor::getOutboundSessionId(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    auto it = outboundSessions_.find(roomId);
    if (it == outboundSessions_.end()) return {};
    return it->second.sessionId;
}

bool Decryptor::hasOutboundSession(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    return outboundSessions_.find(roomId) != outboundSessions_.end();
}

void Decryptor::dropOutboundSession(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    auto it = outboundSessions_.find(roomId);
    if (it != outboundSessions_.end()) {
        if (it->second.session) {
            olm_clear_outbound_group_session(
                olm_outbound_group_session(it->second.session));
            free(it->second.session);
        }
        outboundSessions_.erase(it);
    }
}

} // namespace progressive::desktop
