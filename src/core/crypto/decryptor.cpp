// src/core/crypto/decryptor.cpp — E2EE coordinator (Olm + Megolm).

#include "decryptor.hpp"
#include "olm_account.hpp"

#include <progressive/olm.hpp>
#include <olm/olm.h>
#include <olm/outbound_group_session.h>

#include "../http_client.hpp"
#include "../debug_log.hpp"
#include <simdjson.h>
#include <string_view>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <vector>
#include <random>
#include <atomic>
#include <chrono>

namespace progressive::desktop {

static std::atomic<uint64_t> g_txnCounter{0};

Decryptor::Decryptor()
    : account_(std::make_unique<OlmAccountStore>()),
      megolm_(std::make_unique<MegolmStore>()) {}

Decryptor::~Decryptor() = default;

bool Decryptor::init(const std::string& accountPickle, const std::string& pickleKey,
                      bool shared) {
    if (!accountPickle.empty()) {
        if (!account_->load(accountPickle, pickleKey)) {
            return account_->create();
        }
        account_->setShared(shared);
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

void Decryptor::markDevicesStale(const std::vector<std::string>& userIds) {
    std::lock_guard<std::mutex> lk(staleMtx_);
    for (const auto& uid : userIds) {
        if (staleDeviceUsers_.size() >= 1000) {
            static bool warned = false;
            if (!warned) {
                LOG(LogChannel::E2EE, "markDevicesStale: cap 1000 reached, dropping further entries");
                warned = true;
            }
            break;
        }
        staleDeviceUsers_.insert(uid);
    }
}

bool Decryptor::isDeviceStale(const std::string& userId) {
    std::lock_guard<std::mutex> lk(staleMtx_);
    return staleDeviceUsers_.count(userId) > 0;
}

void Decryptor::clearStale(const std::string& userId) {
    std::lock_guard<std::mutex> lk(staleMtx_);
    staleDeviceUsers_.erase(userId);
}

DecryptionResult Decryptor::decryptMegolmEvent(const std::string& roomId,
                                                  const std::string& senderId,
                                                  const std::string& contentJson,
                                                  const std::string& eventId,
                                                  int64_t originServerTs) {
    DecryptionResult r;
    simdjson::dom::parser mp;
    auto doc = mp.parse(contentJson);
    if (doc.error() != simdjson::SUCCESS) {
        r.error = "failed to parse megolm encrypted content";
        return r;
    }
    auto val = doc.value();
    auto algoStr = val["algorithm"].get_string();
    if (algoStr.error() != simdjson::SUCCESS) {
        r.error = "missing algorithm";
        return r;
    }
    std::string algorithm(algoStr.value());
    if (algorithm != "m.megolm.v1.aes-sha2" && algorithm != "m.megolm.v2.aes-sha2") {
        r.error = "unsupported algorithm: " + algorithm;
        return r;
    }
    auto sk = val["sender_key"].get_string();
    auto sid = val["session_id"].get_string();
    auto ct = val["ciphertext"].get_string();
    if (sk.error() != simdjson::SUCCESS || sid.error() != simdjson::SUCCESS ||
        ct.error() != simdjson::SUCCESS) {
        r.error = "missing sender_key/session_id/ciphertext";
        return r;
    }
    std::string senderKey(sk.value());
    std::string sessionId(sid.value());
    std::string ciphertext(ct.value());

    if (!megolm_->hasSession(roomId, senderKey, sessionId)) {
        r.error = "no megolm session — waiting for room_key";
        LOG(LogChannel::E2EE, "decryptMegolmEvent: no session room=%.40s eid=%s — saving to pending",
            roomId.c_str(), eventId.c_str());
        PendingEncryptedEvent p;
        p.roomId = roomId;
        p.senderKey = senderKey;
        p.sessionId = sessionId;
        p.ciphertext = ciphertext;
        p.senderId = senderId;
        p.eventId = eventId;
        p.originServerTs = originServerTs;
        megolm_->addPending(p);
        requestRoomKey(roomId, senderId, senderKey, sessionId);
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
    simdjson::dom::parser rp;
    auto doc = rp.parse(contentJson);
    if (doc.error() != simdjson::SUCCESS) return false;
    auto val = doc.value();

    auto rid = val["room_id"].get_string();
    auto sid = val["session_id"].get_string();
    auto skey = val["session_key"].get_string();
    if (rid.error() != simdjson::SUCCESS || sid.error() != simdjson::SUCCESS ||
        skey.error() != simdjson::SUCCESS) {
        LOG(LogChannel::E2EE, "handleRoomKey: FAILED — missing required fields");
        return false;
    }
    std::string roomId(rid.value());
    std::string sessionId(sid.value());
    std::string sessionKey(skey.value());

    auto sk = val["sender_key"].get_string();
    std::string senderKey;
    if (sk.error() == simdjson::SUCCESS) {
        senderKey = std::string(sk.value());
    } else {
        auto keys = val["keys"].get_object();
        if (keys.error() == simdjson::SUCCESS) {
            for (auto [k, v] : keys.value()) {
                std::string kStr(k);
                if (kStr.find("curve25519") != std::string::npos) {
                    auto kv = v.get_string();
                    if (kv.error() == simdjson::SUCCESS) senderKey = std::string(kv.value());
                    break;
                }
            }
        }
    }
    LOG(LogChannel::E2EE, "handleRoomKey: room=%.40s sid=%.20s sk=%.20s senderKey=%.20s",
        roomId.c_str(), sessionId.c_str(), sessionKey.c_str(), senderKey.c_str());
    if (senderKey.empty()) {
        LOG(LogChannel::E2EE, "handleRoomKey: FAILED — no sender_key");
        return false;
    }
    bool ok = megolm_->addInboundSession(roomId, senderKey, sessionId, sessionKey);
    LOG(LogChannel::E2EE, "handleRoomKey: addInboundSession=%d", ok ? 1 : 0);
    if (ok) processPending(roomId, senderKey, sessionId);
    return ok;
}

void Decryptor::processPending(const std::string& roomId,
                               const std::string& senderKey,
                               const std::string& sessionId) {
    auto pending = megolm_->takePendingForSession(roomId, senderKey, sessionId);
    LOG(LogChannel::E2EE, "processPending: %zu pending events for room=%.40s",
        pending.size(), roomId.c_str());
    if (pending.empty()) {
        std::fprintf(stderr, "[E2EE] processPending: NO MATCH for room=%.40s sid=%.20s sk=%.20s\n",
            roomId.c_str(), sessionId.c_str(), senderKey.c_str());
        return;
    }

    std::lock_guard<std::mutex> lk(reDecryptedMtx_);
    for (const auto& p : pending) {
        auto plaintext = megolm_->decrypt(p.roomId, p.senderKey, p.sessionId, p.ciphertext);
        if (!plaintext.empty()) {
            LOG(LogChannel::E2EE, "processPending: DECRYPTED eid=%s", p.eventId.c_str());
        } else {
            LOG(LogChannel::E2EE, "processPending: FAILED eid=%s", p.eventId.c_str());
            continue;
        }
        ReDecryptedEvent evt;
        evt.roomId = p.roomId;
        evt.eventId = p.eventId;
        evt.plaintext = std::move(plaintext);
        evt.senderId = p.senderId;
        evt.originServerTs = p.originServerTs;
        reDecryptedEvents_.push_back(std::move(evt));
    }
}

std::vector<ReDecryptedEvent> Decryptor::takeDecryptedEvents() {
    std::lock_guard<std::mutex> lk(reDecryptedMtx_);
    std::vector<ReDecryptedEvent> out;
    out.swap(reDecryptedEvents_);
    return out;
}

// ---- Device key upload body builder ----

std::string Decryptor::signCanonicalJson(const std::string& canonicalJson) {
    return account_->sign(canonicalJson);
}

std::string Decryptor::buildKeysUploadBody(const std::string& userId,
                                               const std::string& deviceId,
                                               int oneTimeKeyCount,
                                               bool includeDeviceKeys) {
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

    // Insert signatures into device_keys before the closing }
    std::string deviceKeysSigned = deviceKeysCanonical;
    deviceKeysSigned.pop_back();  // remove trailing }
    deviceKeysSigned += ",\"signatures\":{\""
        + userId + "\":{\"ed25519:" + deviceId + "\":\"" + signature + "\"}}}";

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

                auto innerObj = field.value.get_object();
                if (innerObj.error() == simdjson::SUCCESS) {
                    // Nested format: {"curve25519":{"AAAAqg":"<key>","AAAAqQ":"<key>"}}
                    // This is what libolm returns since the JSON retrieval fix.
                    for (auto innerField : innerObj.value()) {
                        std::string innerKey(innerField.key);
                        auto innerVal = innerField.value.get_string();
                        if (innerVal.error() != simdjson::SUCCESS) continue;

                        std::string keyObj = "{\"key\":\"" + std::string(innerVal.value()) + "\"}";
                        std::string sig = signCanonicalJson(keyObj);
                        std::string signedKey = "signed_curve25519:" + innerKey;
                        if (!firstOtk) otkSigned << ",";
                        firstOtk = false;
                        otkSigned << "\"" << signedKey << "\":"
                                  << "{\"key\":\"" << std::string(innerVal.value()) << "\","
                                  << "\"signatures\":{\""
                                  << userId << "\":{\"ed25519:" << deviceId << "\":\"" << sig << "\"}}}";
                    }
                } else {
                    // Legacy flat format: {"curve25519:AAAAqg":"<key>","curve25519:BBBB":"<key>"}
                    auto valStr = field.value.get_string();
                    if (valStr.error() != simdjson::SUCCESS) continue;

                    std::string keyObj = "{\"key\":\"" + std::string(valStr.value()) + "\"}";
                    std::string sig = signCanonicalJson(keyObj);
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
    }
    otkSigned << "}";

    // 5. Assemble the full /keys/upload body
    std::ostringstream body;
    body << "{";
    if (includeDeviceKeys) {
        body << "\"device_keys\":" << deviceKeysSigned << ",";
    }
    body << "\"one_time_keys\":" << otkSigned.str()
         << "}";
    return body.str();
}

void Decryptor::markOneTimeKeysPublished() {
    if (account_) account_->markOneTimeKeysPublished();
}

// ---- Olm 1:1 inbound session management ----

std::string Decryptor::handleOlmEncryptedToDevice(const std::string& senderId,
                                                       const std::string& contentJson) {
    // m.room.encrypted to-device content (Olm 1:1):
    //   {"algorithm":"m.olm.v1.curve25519-aes-sha2","ciphertext":
    //    {"<our_curve25519>":{"body":"<base64>","type":0}},"sender_key":"<their_curve25519>"}
    simdjson::dom::parser op;
    auto doc = op.parse(contentJson);
    if (doc.error() != simdjson::SUCCESS) return {};
    auto val = doc.value();

    auto algoStr = val["algorithm"].get_string();
    if (algoStr.error() != simdjson::SUCCESS ||
        std::string(algoStr.value()) != "m.olm.v1.curve25519-aes-sha2") {
        LOG(LogChannel::E2EE, "Olm: wrong algorithm");
        return {};
    }

    auto sk = val["sender_key"].get_string();
    if (sk.error() != simdjson::SUCCESS) {
        LOG(LogChannel::E2EE, "Olm: no sender_key");
        return {};
    }
    std::string senderKey(sk.value());

    std::string ourCurve = account_->curve25519Key();
    if (ourCurve.empty()) {
        LOG(LogChannel::E2EE, "Olm: no our curve25519 key");
        return {};
    }

    auto ct = val["ciphertext"];
    if (ct.error() != simdjson::SUCCESS) {
        LOG(LogChannel::E2EE, "Olm: no ciphertext");
        return {};
    }
    auto ourEntry = ct.value()[ourCurve];
    if (ourEntry.error() != simdjson::SUCCESS) {
        LOG(LogChannel::E2EE, "Olm: our key not found in ciphertext");
        LOG(LogChannel::E2EE, "Olm: our curve25519=%s", ourCurve.c_str());
        auto ctObj = ct.value().get_object();
        if (ctObj.error() == simdjson::SUCCESS) {
            for (auto entry : ctObj.value()) {
                LOG(LogChannel::E2EE, "Olm: ciphertext has key=%s",
                    std::string(entry.key).c_str());
            }
        }
        return {};
    }

    auto bodyStr = ourEntry.value()["body"].get_string();
    auto typeNum = ourEntry.value()["type"].get_int64();
    if (bodyStr.error() != simdjson::SUCCESS) {
        LOG(LogChannel::E2EE, "Olm: empty body");
        return {};
    }
    std::string body(bodyStr.value());
    std::fprintf(stderr, "[E2EE] OLMBODY: %s\n", body.c_str());
    int msgType = (typeNum.error() == simdjson::SUCCESS) ? static_cast<int>(typeNum.value()) : 0;

    std::fprintf(stderr, "[E2EE] Olm cipherObj: parsed via simdjson\n");
    std::fprintf(stderr, "[E2EE] DBG1: body size=%zu\n", body.size());
    std::fprintf(stderr, "[E2EE] DBG2: body size=%zu empty=%d\n", body.size(), body.empty() ? 1 : 0);
    std::fprintf(stderr, "[E2EE] DBG3: typeStr='%d'\n", msgType);
    std::fprintf(stderr, "[E2EE] DBG4: msgType=%d\n", msgType);

    if (body.empty()) {
        LOG(LogChannel::E2EE, "Olm: empty body in ciphertext object");
        return {};
    }

    // Try to find an existing OlmSession for this sender.
    // If none, create one from the pre-key message (type 0).
    std::fprintf(stderr, "[E2EE] DBG5: entering lock\n");
    std::lock_guard<std::mutex> lk(olmMtx_);
    std::fprintf(stderr, "[E2EE] DBG6: lock acquired\n");
    std::string plaintext;

    progressive::OlmSession session;
    auto* underlyingAccount = static_cast<progressive::OlmAccount*>(account_->rawAccount());
    std::fprintf(stderr, "[E2EE] DBG7: account ready\n");

    if (msgType == 0) {
        // Pre-key message — create inbound session, then decrypt
        // createInbound mutates the buffer via (void*) cast in olm.cpp:226.
        // Must pass a copy so the original body remains intact for decrypt.
        std::string msgCopy = body;
        std::fprintf(stderr, "[E2EE] DBG8: pre-key branch, calling createInbound bodySize=%zu\n", msgCopy.size());
        auto result = session.createInbound(*underlyingAccount, msgCopy);
        std::fprintf(stderr, "[E2EE] DBG9: createInbound success=%d\n", result.success ? 1 : 0);
        if (!result.success) {
            LOG(LogChannel::E2EE, "Olm: createInbound Olm session FAILED");
            auto* raw = static_cast<::OlmSession*>(session.rawSession());
            std::fprintf(stderr, "[E2EE] createInbound libolm error: %s\n",
                ::olm_session_last_error(raw) ? ::olm_session_last_error(raw) : "(null)");
            return {};
        }
        // After createInbound, decrypt the ORIGINAL message body
        std::fprintf(stderr, "[E2EE] DBG10: calling decrypt type 0\n");
        auto decResult = session.decrypt(body, 0);
        std::fprintf(stderr, "[E2EE] DBG11: decrypt success=%d dataSize=%zu\n",
            decResult.success ? 1 : 0, decResult.data.size());
        if (!decResult.success) {
            LOG(LogChannel::E2EE, "Olm: decrypt after createInbound FAILED");
            return {};
        }
        plaintext = decResult.data;
        std::fprintf(stderr, "[E2EE] DBG12: plaintext copied size=%zu\n", plaintext.size());

        std::fprintf(stderr, "[E2EE] DBG13: calling pickle\n");
        auto pickleResult = session.pickle("");
        std::fprintf(stderr, "[E2EE] DBG14: pickle success=%d size=%zu\n",
            pickleResult.success ? 1 : 0, pickleResult.data.size());
        if (pickleResult.success) {
            auto& vec = olmSessions_[senderKey];
            bool dup = false;
            for (const auto& existing : vec) {
                if (existing == pickleResult.data) { dup = true; break; }
            }
            if (!dup) vec.push_back(pickleResult.data);
            LOG(LogChannel::E2EE, "Olm: saved session pickle for sender=%s (total=%zu)",
                senderKey.c_str(), vec.size());
        }
    } else {
        auto it = olmSessions_.find(senderKey);
        if (it == olmSessions_.end() || it->second.empty()) {
            LOG(LogChannel::E2EE, "Olm: no saved session for sender=%s — cannot decrypt type %d",
                senderKey.c_str(), msgType);
            return {};
        }
        bool decrypted = false;
        for (size_t i = 0; i < it->second.size(); ++i) {
            progressive::OlmSession sess;
            // libolm mutates the pickle buffer in-place via (void*) cast — pass a copy
            std::string pickleCopy = it->second[i];
            auto unpickleResult = sess.unpickle("", pickleCopy);
            if (!unpickleResult.success) {
                LOG(LogChannel::E2EE, "Olm: unpickle failed for sender=%s idx=%zu (keeping entry)",
                    senderKey.c_str(), i);
                continue;
            }
            auto decResult = sess.decrypt(body, 1);
            if (!decResult.success) {
                continue;
            }
            plaintext = decResult.data;
            auto rePickle = sess.pickle("");
            if (rePickle.success) {
                it->second[i] = rePickle.data;
            }
            decrypted = true;
            break;
        }
        if (!decrypted) {
            LOG(LogChannel::E2EE, "Olm: decrypt type %d FAILED for sender=%s (tried %zu sessions, keeping all)",
                msgType, senderKey.c_str(), it->second.size());
            return {};
        }
    }

    // If we got plaintext, it's a JSON object like:
    //   {"type":"m.room_key","content":{...}}
    // If type == "m.room_key", call handleRoomKey.
    if (!plaintext.empty()) {
        simdjson::dom::parser pp;
        auto pd = pp.parse(plaintext);
        if (pd.error() == simdjson::SUCCESS) {
            auto t = pd.value()["type"].get_string();
            if (t.error() == simdjson::SUCCESS &&
                std::string_view(t.value()) == "m.room_key") {
                std::fprintf(stderr, "[E2EE] Olm plaintext: size=%zu full='%.400s'\n",
                    plaintext.size(), plaintext.c_str());
                LOG(LogChannel::E2EE, "Olm: inner type=m.room_key — calling handleRoomKey (simdjson)");
                auto cr = pd.value()["content"];
                if (cr.error() == simdjson::SUCCESS) {
                    std::string innerContent = simdjson::to_string(cr.value());
                    if (innerContent.find("\"sender_key\"") == std::string::npos) {
                        innerContent.insert(innerContent.size() - 1,
                            ",\"sender_key\":\"" + senderKey + "\"");
                    }
                    handleRoomKey(innerContent);
                }
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
    std::random_device rd;
    std::generate(random.begin(), random.end(), [&]() { return static_cast<uint8_t>(rd()); });
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
    // Import outbound session as inbound so we can decrypt our own message echoes.
    megolm_->addInboundSession(roomId, curve25519Key(), sessionId, sessionKey);
    s.messageIndex = 0;
    outboundSessions_[roomId] = std::move(s);
    roomKeysShared_[roomId] = false;
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

    auto* olmSession = static_cast<::OlmOutboundGroupSession*>(it->second.session);
    // libolm overwrites the message buffer — copy plaintext
    size_t ciphertextLen = olm_group_encrypt_message_length(olmSession, plaintextEventJson.size());
    std::vector<uint8_t> ciphertext(ciphertextLen);
    size_t ret = olm_group_encrypt(olmSession,
        reinterpret_cast<uint8_t*>(const_cast<char*>(plaintextEventJson.data())),
        plaintextEventJson.size(),
        ciphertext.data(), ciphertextLen);
    if (ret == olm_error()) return {};

    // Build m.room.encrypted content
    std::string ciphertextB64(ciphertext.begin(), ciphertext.begin() + ret);
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
    roomKeysShared_.erase(roomId);
}

bool Decryptor::roomKeyShared(const std::string& roomId) const {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    auto it = roomKeysShared_.find(roomId);
    return it != roomKeysShared_.end() && it->second;
}

void Decryptor::markRoomKeyShared(const std::string& roomId) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    roomKeysShared_[roomId] = true;
}

// ---- Room key sharing (full E2EE outbound) ----

// Helper: build auth headers for HTTP calls.
static std::unordered_map<std::string, std::string> makeAuthHeaders(const std::string& token) {
    return {{"Authorization", "Bearer " + token},
            {"Content-Type", "application/json"}};
}

// Helper: extract a string field from JSON (simdjson DOM).
static std::string domGetString(simdjson::dom::element parent, std::string_view key) {
    auto r = parent[key].get_string();
    if (r.error() == simdjson::SUCCESS) return std::string(r.value());
    return {};
}

bool Decryptor::shareRoomKey(const std::string& roomId,
                                const std::vector<std::string>& userIds,
                                const std::string& ourUserId,
                                const std::string& ourDeviceId,
                                const std::string& homeserverUrl,
                                const std::string& accessToken) {
    if (!isInitialized()) {
        std::fprintf(stderr, "[e2ee] shareRoomKey: not initialized\n");
        return false;
    }
    if (!hasOutboundSession(roomId)) {
        std::fprintf(stderr, "[e2ee] shareRoomKey: no outbound session for %s\n", roomId.c_str());
        return false;
    }

    auto ourCurve = curve25519Key();
    auto ourEd = ed25519Key();
    std::string sessionId = getOutboundSessionId(roomId);
    std::string sessionKey = getOutboundSessionKey(roomId);
    if (sessionKey.empty()) {
        std::fprintf(stderr, "[e2ee] shareRoomKey: empty session key\n");
        return false;
    }

    auto hdrs = makeAuthHeaders(accessToken);

    std::fprintf(stderr, "[e2ee] shareRoomKey: room=%.30s users=%zu\n",
                 roomId.c_str(), userIds.size());
    LOG(LogChannel::E2EE, "shareRoomKey: ourUserId=%s ourDeviceId=%s userIds=[%s]",
        ourUserId.c_str(), ourDeviceId.c_str(),
        [&]() { std::string s; for (size_t i=0; i<userIds.size(); ++i) {
            if (i) s += ","; s += userIds[i]; } return s; }().c_str());

    for (const auto& uid : userIds) {
        if (isDeviceStale(uid)) {
            LOG(LogChannel::E2EE, "shareRoomKey: user %s has stale device keys — querying fresh",
                uid.c_str());
            clearStale(uid);
        }
    }

    // Step 1: Query device keys for all room members.
    std::ostringstream queryBody;
    queryBody << "{\"device_keys\":{";
    bool first = true;
    for (const auto& uid : userIds) {
        if (uid == ourUserId) continue;  // skip self
        if (!first) queryBody << ",";
        first = false;
        queryBody << "\"" << uid << "\":[]";
    }
    queryBody << "}}";

    LOG(LogChannel::E2EE, "shareRoomKey: queryBody=%s", queryBody.str().c_str());

    auto queryResp = httpPost(homeserverUrl + "/_matrix/client/v3/keys/query",
                              queryBody.str(), hdrs, 30000);
    if (!queryResp.success) {
        LOG(LogChannel::E2EE, "shareRoomKey: keys/query FAILED http=%d bodyLen=%zu",
            queryResp.statusCode, queryResp.body.size());
        std::fprintf(stderr, "[e2ee] keys/query failed: %s\n", queryResp.errorMessage.c_str());
        return false;
    }

    LOG(LogChannel::E2EE, "shareRoomKey: keys/query ok http=%d bodyLen=%zu body=%.500s",
        queryResp.statusCode, queryResp.body.size(), queryResp.body.c_str());

    // Parse the response to extract device keys for each user.
    // Response format:
    //   {"device_keys":{"@user:server":{"device_id":{"algorithms":[...],
    //    "device_id":"...","keys":{"curve25519:dev":"...","ed25519:dev":"..."},
    //    "signatures":{...}}}},"failures":{}}
    simdjson::dom::parser parser;
    auto rootResult = parser.parse(queryResp.body);
    if (rootResult.error() != simdjson::SUCCESS) {
        std::fprintf(stderr, "[e2ee] keys/query response parse failed\n");
        return false;
    }

    struct DeviceInfo {
        std::string userId;
        std::string deviceId;
        std::string curve25519;
        std::string ed25519;
    };
    std::vector<DeviceInfo> devices;

    auto deviceKeysResult = rootResult.value()["device_keys"].get_object();
    if (deviceKeysResult.error() == simdjson::SUCCESS) {
        for (auto userField : deviceKeysResult.value()) {
            std::string uid(userField.key);
            size_t userDeviceCount = 0;
            auto userDevices = userField.value.get_object();
            if (userDevices.error() != simdjson::SUCCESS) continue;
            for (auto devField : userDevices.value()) {
                DeviceInfo info;
                info.userId = uid;
                info.deviceId = std::string(devField.key);
                if (info.deviceId == ourDeviceId && uid == ourUserId) continue;
                info.curve25519 = domGetString(devField.value, "curve25519:" + info.deviceId);
                // Extract curve25519 + ed25519 from keys object
                auto keysResult = devField.value["keys"].get_object();
                if (keysResult.error() == simdjson::SUCCESS) {
                    auto keysObj = keysResult.value();
                    for (auto k : keysObj) {
                        std::string kKey(k.key);
                        if (kKey.find("curve25519") != std::string::npos && info.curve25519.empty()) {
                            auto v = k.value.get_string();
                            if (v.error() == simdjson::SUCCESS) info.curve25519 = std::string(v.value());
                        }
                        if (kKey.find("ed25519") != std::string::npos) {
                            auto v = k.value.get_string();
                            if (v.error() == simdjson::SUCCESS) info.ed25519 = std::string(v.value());
                        }
                    }
                }
                if (!info.curve25519.empty()) {
                    devices.push_back(info);
                    userDeviceCount++;
                    std::fprintf(stderr, "[e2ee] found device: %s/%s curve=%s...\n",
                                 uid.c_str(), info.deviceId.c_str(),
                                 info.curve25519.substr(0, 8).c_str());
                }
            }
            LOG(LogChannel::E2EE, "shareRoomKey: user=%s deviceCount=%zu",
                uid.c_str(), userDeviceCount);
        }
    }

    std::fprintf(stderr, "[e2ee] shareRoomKey: keys/query ok devices=%zu\n", devices.size());

    if (devices.empty()) {
        std::fprintf(stderr, "[e2ee] no devices to share room_key with\n");
        return false;
    }

    // Step 2: Claim one-time keys for each device.
    std::ostringstream claimBody;
    claimBody << "{\"one_time_keys\":{";
    first = true;
    for (const auto& d : devices) {
        if (!first) claimBody << ",";
        first = false;
        claimBody << "\"" << d.userId << "\":{\""
                  << d.deviceId << "\":\"signed_curve25519\"}";
    }
    claimBody << "}}";

    auto claimResp = httpPost(homeserverUrl + "/_matrix/client/v3/keys/claim",
                              claimBody.str(), hdrs, 15000);
    if (!claimResp.success) {
        std::fprintf(stderr, "[e2ee] keys/claim failed: %s\n", claimResp.errorMessage.c_str());
        return false;
    }

    // Parse the response to extract claimed one-time keys.
    // Response: {"one_time_keys":{"@user:server":{"device_id":
    //   {"signed_curve25519:AAAA":{"key":"...","signatures":{...}}}}}}
    simdjson::dom::parser claimParser;
    auto claimRoot = claimParser.parse(claimResp.body);
    if (claimRoot.error() != simdjson::SUCCESS) {
        std::fprintf(stderr, "[e2ee] keys/claim response parse failed\n");
        return false;
    }

    // For each device, find the claimed one-time key.
    struct ClaimedKey {
        std::string userId;
        std::string deviceId;
        std::string oneTimeKey;  // the actual key value
    };
    std::vector<ClaimedKey> claimedKeys;

    auto otkResult = claimRoot.value()["one_time_keys"].get_object();
    if (otkResult.error() == simdjson::SUCCESS) {
        for (auto userField : otkResult.value()) {
            std::string uid(userField.key);
            auto userDevs = userField.value.get_object();
            if (userDevs.error() != simdjson::SUCCESS) continue;
            for (auto devField : userDevs.value()) {
                std::string devId(devField.key);
                // The value is an object with one key: signed_curve25519:XXXX
                auto keyObj = devField.value.get_object();
                if (keyObj.error() != simdjson::SUCCESS) continue;
                for (auto k : keyObj.value()) {
                    ClaimedKey ck;
                    ck.userId = uid;
                    ck.deviceId = devId;
                    // The value has a "key" field
                    ck.oneTimeKey = domGetString(k.value, "key");
                    if (ck.oneTimeKey.empty()) {
                        // Maybe the value IS the key directly (some servers)
                        auto keyStr = k.value.get_string();
                        if (keyStr.error() == simdjson::SUCCESS) {
                            ck.oneTimeKey = std::string(keyStr.value());
                        }
                    }
                    if (!ck.oneTimeKey.empty()) {
                        claimedKeys.push_back(ck);
                    }
                    break;  // only one key per device
                }
            }
        }
    }

    std::fprintf(stderr, "[e2ee] claimed %zu one-time keys (had %zu devices)\n",
                 claimedKeys.size(), devices.size());

    if (claimedKeys.empty()) {
        std::fprintf(stderr, "[e2ee] no one-time keys claimed — can't share room_key\n");
        return false;
    }

    // Step 3: For each claimed key, create OlmSession outbound + encrypt m.room_key.
    // Build the /sendToDevice/m.room.encrypted body:
    //   {"messages":{"@user:server":{"device_id":{"algorithm":"m.olm.v1.curve25519-aes-sha2",
    //    "ciphertext":{"<their_curve>":{"body":"<base64>","type":0}},
    //    "sender_key":"<our_curve>"}}}}
    std::ostringstream sendBody;
    sendBody << "{\"messages\":{";
    first = true;
    int shared = 0;

    for (const auto& ck : claimedKeys) {
        // Find the matching device info for ed25519
        std::string theirEd;
        for (const auto& d : devices) {
            if (d.userId == ck.userId && d.deviceId == ck.deviceId) {
                theirEd = d.ed25519;
                break;
            }
        }
        std::string theirCurve;
        for (const auto& d : devices) {
            if (d.userId == ck.userId && d.deviceId == ck.deviceId) {
                theirCurve = d.curve25519;
                break;
            }
        }
        if (theirCurve.empty()) continue;
        if (theirEd.empty()) {
            std::fprintf(stderr, "[e2ee] shareRoomKey: no ed25519 for %s/%s — skipping\n",
                         ck.userId.c_str(), ck.deviceId.c_str());
            continue;
        }

        // Create OlmSession outbound
        progressive::OlmSession session;
        auto* underlyingAccount = static_cast<progressive::OlmAccount*>(account_->rawAccount());
        auto sessResult = session.createOutbound(*underlyingAccount, theirCurve, ck.oneTimeKey);
        if (!sessResult.success) {
            std::fprintf(stderr, "[e2ee] createOutbound failed for %s/%s\n",
                         ck.userId.c_str(), ck.deviceId.c_str());
            continue;
        }

        // Build the m.room_key plaintext JSON
        std::string roomKeyContent = "{\"algorithm\":\"m.megolm.v1.aes-sha2\","
            "\"room_id\":\"" + roomId + "\","
            "\"session_id\":\"" + sessionId + "\","
            "\"session_key\":\"" + sessionKey + "\"}";

        std::fprintf(stderr, "[e2ee] shareRoomKey: roomKeyContent=%s\n",
                     roomKeyContent.c_str());

        // Wrap it as a to-device event JSON:
        // {"type":"m.room_key","content":{...},"sender":"<our_user_id>","keys":{"ed25519":"<our_ed>"}}
        std::string plaintext = "{\"type\":\"m.room_key\",\"content\":" + roomKeyContent +
            ",\"sender\":\"" + ourUserId + "\""
            ",\"recipient\":\"" + ck.userId + "\""
            ",\"keys\":{\"ed25519\":\"" + ourEd + "\"}"
            ",\"recipient_keys\":{\"ed25519\":\"" + theirEd + "\"}}";

        // Encrypt with OlmSession
        auto encResult = session.encrypt(plaintext);
        if (!encResult.success || encResult.data.empty()) {
            std::fprintf(stderr, "[e2ee] Olm encrypt failed for %s/%s\n",
                         ck.userId.c_str(), ck.deviceId.c_str());
            continue;
        }

        std::fprintf(stderr, "[e2ee] shareRoomKey: encBody=%.200s\n",
                     encResult.data.c_str());

        // Build the per-device ciphertext entry
        if (!first) sendBody << ",";
        first = false;
        sendBody << "\"" << ck.userId << "\":{"
                 << "\"" << ck.deviceId << "\":{"
                 << "\"algorithm\":\"m.olm.v1.curve25519-aes-sha2\","
                 << "\"ciphertext\":{\"" << theirCurve << "\":{"
                 << "\"body\":\"" << encResult.data << "\","
                 << "\"type\":0}},"
                 << "\"sender_key\":\"" << ourCurve << "\""
                 << "}}";
        shared++;
    }
    sendBody << "}}";

    std::fprintf(stderr, "[e2ee] shareRoomKey: sendBody=%.600s\n",
                 sendBody.str().c_str());

    if (shared == 0) {
        std::fprintf(stderr, "[e2ee] failed to encrypt room_key for any device\n");
        return false;
    }

    // Step 4: Send m.room.encrypted to-device event.
    std::string txnId = "pdkey" + std::to_string(
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
        + g_txnCounter.fetch_add(1));
    std::string url = homeserverUrl + "/_matrix/client/v3/sendToDevice/m.room.encrypted/" + txnId;
    auto sendResp = httpPut(url, sendBody.str(), hdrs, 15000);
    if (!sendResp.success) {
        std::fprintf(stderr, "[e2ee] sendToDevice failed: %s\n", sendResp.errorMessage.c_str());
        return false;
    }

    std::fprintf(stderr, "[e2ee] shareRoomKey: sendToDevice ok shared=%d\n", shared);

    std::fprintf(stderr, "[e2ee] shared room_key with %d device(s) for room %s\n",
                 shared, roomId.c_str());
    return true;
}

size_t Decryptor::olmSessionCount() {
    std::lock_guard<std::mutex> lk(olmMtx_);
    size_t total = 0;
    for (const auto& [k, v] : olmSessions_) total += v.size();
    return total;
}

void Decryptor::setCryptoContext(const std::string& ourUserId, const std::string& ourDeviceId,
                                  const std::string& homeserverUrl, const std::string& accessToken) {
    ctxUserId_ = ourUserId;
    ctxDeviceId_ = ourDeviceId;
    ctxHomeserver_ = homeserverUrl;
    ctxToken_ = accessToken;
}

void Decryptor::requestRoomKey(const std::string& roomId, const std::string& senderId,
                                const std::string& senderKey, const std::string& sessionId) {
    if (ctxHomeserver_.empty() || ctxToken_.empty() || senderId.empty()) return;
    std::string key = roomId + "|" + sessionId + "|" + senderKey;
    {
        std::lock_guard<std::mutex> lk(requestMtx_);
        if (requestedKeys_.count(key)) return;
        requestedKeys_.insert(key);
    }
    std::string reqId = "pdrkr" + std::to_string(
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
        + g_txnCounter.fetch_add(1));
    std::ostringstream body;
    body << "{\"messages\":{\""
         << senderId << "\":{\"*\":{\"action\":\"request\","
         << "\"body\":{\"algorithm\":\"m.megolm.v1.aes-sha2\","
         << "\"room_id\":\"" << roomId << "\","
         << "\"sender_key\":\"" << senderKey << "\","
         << "\"session_id\":\"" << sessionId << "\"},"
         << "\"request_id\":\"" << reqId << "\","
         << "\"requesting_device_id\":\"" << ctxDeviceId_ << "\"}}}}";
    forceNewOlmSession(senderId, senderKey);  // MUST be first — establishes new Olm session before requesting re-share
    auto hdrs = makeAuthHeaders(ctxToken_);
    std::string url = ctxHomeserver_ + "/_matrix/client/v3/sendToDevice/m.room_key_request/" + reqId;
    auto resp = httpPut(url, body.str(), hdrs, 15000);
    LOG(LogChannel::E2EE, "requestRoomKey: sent for room=%.40s sid=%.20s sender=%s ok=%d status=%d err=%s",
        roomId.c_str(), sessionId.c_str(), senderId.c_str(), resp.success ? 1 : 0,
        resp.statusCode, resp.errorMessage.c_str());
    std::fprintf(stderr, "[e2ee] requestRoomKey: room=%.40s sid=%.20s sender=%s ok=%d status=%d err=%s\n",
                 roomId.c_str(), sessionId.c_str(), senderId.c_str(), resp.success ? 1 : 0,
                 resp.statusCode, resp.errorMessage.c_str());
}

void Decryptor::forceNewOlmSession(const std::string& senderId, const std::string& senderKey) {
    if (ctxHomeserver_.empty() || ctxToken_.empty()) return;

    {
        std::lock_guard<std::mutex> lk(requestMtx_);
        if (forcedOlm_.count(senderKey)) return;
        forcedOlm_.insert(senderKey);
    }

    auto hdrs = makeAuthHeaders(ctxToken_);
    auto ourCurve = curve25519Key();
    auto ourEd = ed25519Key();

    std::string queryBody = "{\"device_keys\":{\"" + senderId + "\":[]}}";
    auto queryResp = httpPost(ctxHomeserver_ + "/_matrix/client/v3/keys/query",
                               queryBody, hdrs, 30000);
    if (!queryResp.success) {
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: keys/query failed for %s status=%d err=%s\n",
                     senderId.c_str(), queryResp.statusCode, queryResp.errorMessage.c_str());
        return;
    }
    simdjson::dom::parser parser;
    auto root = parser.parse(queryResp.body);
    if (root.error() != simdjson::SUCCESS) return;
    std::string theirDeviceId, theirEd;
    auto dkResult = root.value()["device_keys"].get_object();
    if (dkResult.error() == simdjson::SUCCESS) {
        for (auto userField : dkResult.value()) {
            auto userDevices = userField.value.get_object();
            if (userDevices.error() != simdjson::SUCCESS) continue;
            for (auto devField : userDevices.value()) {
                std::string devId(devField.key);
                if (devId == ctxDeviceId_) continue;
                auto keysResult = devField.value["keys"].get_object();
                if (keysResult.error() != simdjson::SUCCESS) continue;
                std::string devCurve, devEd;
                for (auto k : keysResult.value()) {
                    std::string kKey(k.key);
                    if (kKey.find("curve25519") != std::string::npos) {
                        auto v = k.value.get_string();
                        if (v.error() == simdjson::SUCCESS) devCurve = std::string(v.value());
                    }
                    if (kKey.find("ed25519") != std::string::npos) {
                        auto v = k.value.get_string();
                        if (v.error() == simdjson::SUCCESS) devEd = std::string(v.value());
                    }
                }
                if (devCurve == senderKey) {
                    theirDeviceId = devId;
                    theirEd = devEd;
                    break;
                }
            }
        }
    }
    if (theirDeviceId.empty() || theirEd.empty()) {
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: no device found for senderKey=%.20s\n",
                     senderKey.c_str());
        return;
    }

    std::string claimBody = "{\"one_time_keys\":{\""
        + senderId + "\":{\"" + theirDeviceId + "\":\"signed_curve25519\"}}}";
    auto claimResp = httpPost(ctxHomeserver_ + "/_matrix/client/v3/keys/claim",
                               claimBody, hdrs, 15000);
    if (!claimResp.success) {
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: keys/claim failed\n");
        return;
    }
    std::string oneTimeKey;
    auto claimRoot = parser.parse(claimResp.body);
    if (claimRoot.error() == simdjson::SUCCESS) {
        auto otkResult = claimRoot.value()["one_time_keys"].get_object();
        if (otkResult.error() == simdjson::SUCCESS) {
            for (auto userField : otkResult.value()) {
                auto userDevs = userField.value.get_object();
                if (userDevs.error() != simdjson::SUCCESS) continue;
                for (auto devField : userDevs.value()) {
                    auto keyObj = devField.value.get_object();
                    if (keyObj.error() != simdjson::SUCCESS) continue;
                    for (auto k : keyObj.value()) {
                        oneTimeKey = domGetString(k.value, "key");
                        if (oneTimeKey.empty()) {
                            auto keyStr = k.value.get_string();
                            if (keyStr.error() == simdjson::SUCCESS)
                                oneTimeKey = std::string(keyStr.value());
                        }
                        if (!oneTimeKey.empty()) break;
                    }
                }
            }
        }
    }
    if (oneTimeKey.empty()) {
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: no OTK claimed for %s/%s\n",
                     senderId.c_str(), theirDeviceId.c_str());
        return;
    }

    progressive::OlmSession session;
    auto* underlyingAccount = static_cast<progressive::OlmAccount*>(account_->rawAccount());
    auto sessResult = session.createOutbound(*underlyingAccount, senderKey, oneTimeKey);
    if (!sessResult.success) {
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: createOutbound failed\n");
        return;
    }

    std::string plaintext = "{\"type\":\"m.dummy\",\"content\":{},"
        "\"sender\":\"" + ctxUserId_ + "\","
        "\"recipient\":\"" + senderId + "\","
        "\"keys\":{\"ed25519\":\"" + ourEd + "\"},"
        "\"recipient_keys\":{\"ed25519\":\"" + theirEd + "\"}}";

    auto encResult = session.encrypt(plaintext);
    if (!encResult.success || encResult.data.empty()) {
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: Olm encrypt failed\n");
        return;
    }

    auto pickleResult = session.pickle("");
    if (pickleResult.success) {
        std::lock_guard<std::mutex> lk(olmMtx_);
        auto& vec = olmSessions_[senderKey];
        bool dup = false;
        for (const auto& existing : vec) {
            if (existing == pickleResult.data) { dup = true; break; }
        }
        if (!dup) vec.push_back(pickleResult.data);
        std::fprintf(stderr, "[e2ee] forceNewOlmSession: stored outbound session for senderKey=%.20s\n",
                     senderKey.c_str());
    }

    std::string txnId = "pddmy" + std::to_string(
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
        + g_txnCounter.fetch_add(1));
    std::ostringstream sendBody;
    sendBody << "{\"messages\":{\""
             << senderId << "\":{\""
             << theirDeviceId << "\":{\"algorithm\":\"m.olm.v1.curve25519-aes-sha2\","
             << "\"ciphertext\":{\"" << senderKey << "\":{"
             << "\"body\":\"" << encResult.data << "\","
             << "\"type\":0}},"
             << "\"sender_key\":\"" << ourCurve << "\"}}}}";
    std::string url = ctxHomeserver_ + "/_matrix/client/v3/sendToDevice/m.room.encrypted/" + txnId;
    auto sendResp = httpPut(url, sendBody.str(), hdrs, 15000);
    std::fprintf(stderr, "[e2ee] forceNewOlmSession: sent m.dummy to %s/%s ok=%d status=%d err=%s\n",
                 senderId.c_str(), theirDeviceId.c_str(), sendResp.success ? 1 : 0,
                 sendResp.statusCode, sendResp.errorMessage.c_str());
    LOG(LogChannel::E2EE, "forceNewOlmSession: sent m.dummy to %s/%s ok=%d status=%d err=%s",
        senderId.c_str(), theirDeviceId.c_str(), sendResp.success ? 1 : 0,
        sendResp.statusCode, sendResp.errorMessage.c_str());
}

std::string Decryptor::pickleOlmSessions(const std::string& key) {
    std::lock_guard<std::mutex> lk(olmMtx_);
    if (olmSessions_.empty()) return "[]";
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& [senderKey, pickles] : olmSessions_) {
        for (const auto& pickle : pickles) {
            if (!first) os << ",";
            first = false;
            os << "{\"k\":\"" << senderKey << "\",\"v\":\"";
            for (unsigned char c : pickle) {
                static const char hex[] = "0123456789abcdef";
                os << hex[c >> 4] << hex[c & 15];
            }
            os << "\"}";
        }
    }
    os << "]";
    return os.str();
}

bool Decryptor::unpickleOlmSessions(const std::string& key, const std::string& data) {
    std::lock_guard<std::mutex> lk(olmMtx_);
    if (data.empty() || data == "[]") return true;
    (void)key;
    // Parse JSON array with simdjson (fixes bug #11: manual brace-matching
    // parser used find("}}") which never matched single-} object endings,
    // causing only the first Olm session to be loaded)
    simdjson::dom::parser parser;
    auto root = parser.parse(data);
    if (root.error() != simdjson::SUCCESS) return true;
    auto arr = root.value().get_array();
    if (arr.error() != simdjson::SUCCESS) return true;
    for (auto elem : arr.value()) {
        auto obj = elem.get_object();
        if (obj.error() != simdjson::SUCCESS) continue;
        auto k = obj.value()["k"].get_string();
        auto v = obj.value()["v"].get_string();
        if (k.error() != simdjson::SUCCESS || v.error() != simdjson::SUCCESS) continue;
        std::string senderKey(k.value());
        std::string hexPickle(v.value());
        if (hexPickle.size() % 2 != 0) continue;
        std::string pickle;
        for (size_t i = 0; i < hexPickle.size(); i += 2) {
            char h = (char)strtol(hexPickle.substr(i, 2).c_str(), nullptr, 16);
            pickle += h;
        }
        olmSessions_[senderKey].push_back(pickle);
        std::fprintf(stderr, "[e2ee] olm: loaded session %.30s (pickleLen=%zu)\n",
                     senderKey.c_str(), pickle.size());
    }
    size_t total = 0;
    for (const auto& [k, v] : olmSessions_) total += v.size();
    std::fprintf(stderr, "[e2ee] loaded %zu olm session pickles (%zu senders)\n", total, olmSessions_.size());
    return true;
}

std::string Decryptor::pickleOutboundSessions(const std::string& key) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    if (outboundSessions_.empty()) return "[]";
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& [roomId, out] : outboundSessions_) {
        auto* olmSession = static_cast<::OlmOutboundGroupSession*>(out.session);
        if (!olmSession) continue;
        if (!first) os << ",";
        first = false;
        os << "{\"roomId\":\"" << roomId << "\","
           << "\"sessionId\":\"" << out.sessionId << "\","
           << "\"sessionKey\":\"" << out.sessionKey << "\","
           << "\"messageIndex\":" << out.messageIndex << ","
           << "\"shared\":" << (roomKeysShared_[roomId] ? "true" : "false");
        size_t len = olm_pickle_outbound_group_session_length(olmSession);
        if (len > 0) {
            std::vector<uint8_t> pickled(len);
            size_t ret = olm_pickle_outbound_group_session(olmSession, "", 0, pickled.data(), len);
            if (ret != olm_error()) {
                os << ",\"pickle\":\"";
                for (size_t i = 0; i < len; i++) {
                    static const char hex[] = "0123456789abcdef";
                    os << hex[pickled[i] >> 4] << hex[pickled[i] & 15];
                }
                os << "\"";
            }
        }
        os << "}";
    }
    os << "]";
    return os.str();
}

bool Decryptor::unpickleOutboundSessions(const std::string& key, const std::string& data) {
    std::lock_guard<std::mutex> lk(outboundMtx_);
    if (data.empty() || data == "[]") return true;
    (void)key;
    simdjson::dom::parser parser;
    auto root = parser.parse(data);
    if (root.error() != simdjson::SUCCESS) return false;
    auto arr = root.value().get_array();
    if (arr.error() != simdjson::SUCCESS) return false;
    for (auto elem : arr.value()) {
        auto obj = elem.get_object();
        if (obj.error() != simdjson::SUCCESS) continue;
        auto r = obj.value()["roomId"].get_string();
        auto si = obj.value()["sessionId"].get_string();
        auto sk = obj.value()["sessionKey"].get_string();
        auto mi = obj.value()["messageIndex"].get_int64();
        auto sh = obj.value()["shared"].get_bool();
        auto pk = obj.value()["pickle"].get_string();
        if (r.error() != simdjson::SUCCESS || si.error() != simdjson::SUCCESS ||
            sk.error() != simdjson::SUCCESS) continue;
        std::string roomId(r.value());
        std::string hexPickle(pk.error() == simdjson::SUCCESS ? pk.value() : "");
        if (hexPickle.empty() || hexPickle.size() % 2 != 0) continue;
        std::vector<uint8_t> pickledData;
        for (size_t i = 0; i < hexPickle.size(); i += 2) {
            char h = (char)strtol(hexPickle.substr(i, 2).c_str(), nullptr, 16);
            pickledData.push_back((uint8_t)h);
        }
        // MUST pass a copy — libolm consumes the pickled buffer in place (Quirk 8)
        std::vector<uint8_t> pickledCopy = pickledData;
        void* mem = malloc(olm_outbound_group_session_size());
        if (!mem) continue;
        // olm_outbound_group_session zeros the struct (Quirk 7) — call ONCE
        auto* olmSession = olm_outbound_group_session(mem);
        size_t ret = olm_unpickle_outbound_group_session(olmSession, "", 0,
            pickledCopy.data(), pickledCopy.size());
        if (ret == olm_error()) {
            free(mem);
            continue;
        }
        OutboundMegolmSession out;
        out.session = mem;
        out.sessionId = std::string(si.value());
        out.sessionKey = std::string(sk.value());
        out.messageIndex = mi.error() == simdjson::SUCCESS ? (int)mi.value() : 0;
        outboundSessions_[roomId] = std::move(out);
        bool shared = sh.error() == simdjson::SUCCESS ? sh.value() : false;
        roomKeysShared_[roomId] = shared;
        // DEBT(E2EE): stale shared flag if members changed while offline — new
        // members won't get the room_key until session rotation. matrix-rust-sdk
        // re-shares on device_lists:changed; we don't yet. See AGENTS.md gaps.
        LOG(LogChannel::E2EE, "unpickleOutbound: room=%s sid=%.20s shared=%d",
            roomId.c_str(), out.sessionId.c_str(), shared ? 1 : 0);
    }
    return true;
}

} // namespace progressive::desktop
