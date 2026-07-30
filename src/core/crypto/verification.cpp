// src/core/crypto/verification.cpp
#include "verification.hpp"
#include "random.hpp"
#include "../debug_log.hpp"
#include <simdjson.h>
#include <sstream>
#include <iomanip>
#include <olm/olm.h>

namespace progressive::desktop {

std::string cancelCodeToString(CancelCode code) {
    switch (code) {
        case CancelCode::User: return "m.user";
        case CancelCode::Timeout: return "m.timeout";
        case CancelCode::UnknownTransaction: return "m.unknown_transaction";
        case CancelCode::UnknownMethod: return "m.unknown_method";
        case CancelCode::UnexpectedMessage: return "m.unexpected_message";
        case CancelCode::KeyMismatch: return "m.key_mismatch";
        case CancelCode::UserMismatch: return "m.user_mismatch";
        case CancelCode::InvalidMessage: return "m.invalid_message";
        case CancelCode::Accepted: return "m.accepted";
        case CancelCode::Sasmismatch: return "m.mismatched_sas";
        case CancelCode::Other: return "m.unknown";
    }
    return "m.unknown";
}

bool VerificationTransaction::isExpired() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::minutes>(now - startTime).count() >= 10;
}

std::string VerificationManager::generateTransactionId() {
    uint8_t rnd[16];
    fillCryptoRandom(rnd, sizeof(rnd));
    std::ostringstream os;
    os << "pdv_";
    for (int i = 0; i < 16; i++)
        os << std::hex << std::setw(2) << std::setfill('0') << (int)rnd[i];
    return os.str();
}

VerificationTransaction* VerificationManager::startVerification(
    const std::string& otherUserId, const std::string& otherDeviceId,
    const std::string& ourDeviceId, bool toDevice,
    const std::string& roomId, const std::string& requestEventId) {
    auto txn = std::make_unique<VerificationTransaction>();
    txn->transactionId = generateTransactionId();
    txn->otherUserId = otherUserId;
    txn->otherDeviceId = otherDeviceId;
    txn->ourDeviceId = ourDeviceId;
    txn->weInitiated = true;
    txn->state = VerificationState::RequestSent;
    txn->roomId = toDevice ? "" : roomId;
    txn->requestEventId = requestEventId;
    txn->startTime = std::chrono::steady_clock::now();
    auto* ptr = txn.get();
    transactions_.push_back(std::move(txn));
    return ptr;
}

VerificationTransaction* VerificationManager::findTransaction(const std::string& txnId) {
    for (auto& t : transactions_) {
        if (t->transactionId == txnId || t->requestEventId == txnId) return t.get();
    }
    return nullptr;
}

void VerificationManager::removeTransaction(const std::string& txnId) {
    for (auto it = transactions_.begin(); it != transactions_.end(); ++it) {
        if ((*it)->transactionId == txnId) {
            transactions_.erase(it);
            return;
        }
    }
}

std::vector<VerificationTransaction*> VerificationManager::activeTransactions() const {
    std::vector<VerificationTransaction*> result;
    for (auto& t : transactions_) {
        if (t->state != VerificationState::Done &&
            t->state != VerificationState::Cancelled) {
            result.push_back(t.get());
        }
    }
    return result;
}

static std::string domGetString(simdjson::dom::element e, const std::string& path);

VerificationTransaction* VerificationManager::handleEvent(
    const std::string& eventType, const std::string& senderId,
    const std::string& contentJson, const std::string& ourUserId,
    const std::string& ourEd25519, const std::string& ourCurve25519) {

    simdjson::dom::parser p;
    auto doc = p.parse(contentJson);
    if (doc.error() != simdjson::SUCCESS) return nullptr;

    auto val = doc.value();
    auto txnIdResult = val["transaction_id"].get_string();
    std::string txnId = (txnIdResult.error() == simdjson::SUCCESS)
        ? std::string(txnIdResult.value()) : "";

    auto fromDevice = val["from_device"].get_string();
    std::string otherDeviceId = (fromDevice.error() == simdjson::SUCCESS)
        ? std::string(fromDevice.value()) : "";

    if (txnId.empty()) return nullptr;

    VerificationTransaction* txn = nullptr;

    if (eventType == "m.key.verification.request") {
        auto methodsResult = val["methods"].get_array();
        bool hasSas = false;
        if (methodsResult.error() == simdjson::SUCCESS) {
            for (auto m : methodsResult.value()) {
                auto s = m.get_string();
                if (s.error() == simdjson::SUCCESS && std::string(s.value()) == "m.sas.v1") {
                    hasSas = true; break;
                }
            }
        }
        if (!hasSas) return nullptr;

        auto t = std::make_unique<VerificationTransaction>();
        t->transactionId = txnId;
        t->otherUserId = senderId;
        t->otherDeviceId = otherDeviceId;
        t->isIncoming = true;
        t->state = VerificationState::RequestReceived;
        t->startTime = std::chrono::steady_clock::now();
        txn = t.get();
        transactions_.push_back(std::move(t));
        return txn;
    }

    txn = findTransaction(txnId);
    if (!txn) return nullptr;

    if (eventType == "m.key.verification.ready") {
        txn->state = VerificationState::Ready;
    } else if (eventType == "m.key.verification.start") {
        txn->state = VerificationState::Started;
        auto method = val["method"].get_string();
        if (method.error() == simdjson::SUCCESS &&
            std::string(method.value()) == "m.sas.v1") {
            txn->sas = sasCreate();
            txn->startContentJson = contentJson;
        }
    } else if (eventType == "m.key.verification.accept") {
        txn->state = VerificationState::Accepted;
        auto commitResult = val["commitment"].get_string();
        if (commitResult.error() == simdjson::SUCCESS)
            txn->commitment = std::string(commitResult.value());
    } else if (eventType == "m.key.verification.key") {
        auto keyResult = val["key"].get_string();
        if (keyResult.error() == simdjson::SUCCESS) {
            txn->theirSasPubkey = std::string(keyResult.value());
            if (txn->sas.valid)
                sasSetTheirKey(txn->sas, txn->theirSasPubkey);
            if (txn->state == VerificationState::KeySent)
                txn->state = VerificationState::KeyReceived;
            else if (txn->state != VerificationState::KeyReceived)
                txn->state = VerificationState::KeyReceived;
        }
    } else if (eventType == "m.key.verification.mac") {
        if (txn->state == VerificationState::MacSent)
            txn->state = VerificationState::Done;
        else
            txn->state = VerificationState::MacReceived;
    } else if (eventType == "m.key.verification.done") {
        if (txn->state == VerificationState::MacReceived ||
            txn->state == VerificationState::MacSent)
            txn->state = VerificationState::Done;
    } else if (eventType == "m.key.verification.cancel") {
        txn->state = VerificationState::Cancelled;
        auto codeResult = val["code"].get_string();
        if (codeResult.error() == simdjson::SUCCESS) {
            std::string code(codeResult.value());
            if (code == "m.user") txn->cancelCode = CancelCode::User;
            else if (code == "m.timeout") txn->cancelCode = CancelCode::Timeout;
            else txn->cancelCode = CancelCode::Other;
        }
    }

    return txn;
}

// ---- Message builders ----

static std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '"') out += "\\\""; else out += c; }
    return out;
}

std::string VerificationManager::buildRequestContent(const std::string& ourDeviceId,
    const std::string& txnId) const {
    return "{\"from_device\":\"" + esc(ourDeviceId) + "\","
           "\"transaction_id\":\"" + esc(txnId) + "\","
           "\"methods\":[\"m.sas.v1\"],\"timestamp\":" +
           std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count()) + "}";
}

std::string VerificationManager::buildReadyContent(const std::string& ourDeviceId,
    const std::string& txnId) const {
    return "{\"from_device\":\"" + esc(ourDeviceId) + "\","
           "\"transaction_id\":\"" + esc(txnId) + "\","
           "\"methods\":[\"m.sas.v1\"]}";
}

std::string VerificationManager::buildStartContent(const std::string& ourDeviceId,
    const std::string& txnId) const {
    return "{\"from_device\":\"" + esc(ourDeviceId) + "\","
           "\"transaction_id\":\"" + esc(txnId) + "\","
           "\"method\":\"m.sas.v1\","
           "\"key_agreement_protocols\":[\"curve25519\"],"
           "\"hashes\":[\"sha256\"],"
           "\"message_authentication_codes\":[\"hkdf-hmac-sha256\"],"
           "\"short_authentication_string\":[\"emoji\",\"decimal\"]}";
}

std::string VerificationManager::buildAcceptContent(const std::string& txnId,
    const std::string& commitment) const {
    return "{\"transaction_id\":\"" + esc(txnId) + "\","
           "\"key_agreement_protocol\":\"curve25519\","
           "\"hash\":\"sha256\","
           "\"message_authentication_code\":\"hkdf-hmac-sha256\","
           "\"short_authentication_string\":[\"emoji\",\"decimal\"],"
           "\"commitment\":\"" + esc(commitment) + "\"}";
}

std::string VerificationManager::buildKeyContent(const std::string& txnId,
    const std::string& sasPubkey) const {
    return "{\"transaction_id\":\"" + esc(txnId) + "\","
           "\"key\":\"" + esc(sasPubkey) + "\"}";
}

std::string VerificationManager::buildMacContent(const std::string& txnId,
    const std::string& ourDeviceId, const std::string& ourEd25519,
    const std::string& ourCurve25519, SasSession& sas) const {
    std::string infoKey = macInfo(txnId, ourDeviceId, ourEd25519, ourCurve25519);
    std::string ed25519mac = sasCalculateMac(sas, ourEd25519, infoKey);
    std::string curve25519mac = sasCalculateMac(sas, ourCurve25519, infoKey);
    return "{\"transaction_id\":\"" + esc(txnId) + "\","
           "\"mac\":{\"ed25519:" + esc(ourDeviceId) + "\":\"" + esc(ed25519mac) + "\","
           "\"curve25519:" + esc(ourDeviceId) + "\":\"" + esc(curve25519mac) + "\"},"
           "\"keys\":\"curve25519:" + esc(ourDeviceId) + ",ed25519:" + esc(ourDeviceId) + "\"}";
}

std::string VerificationManager::buildDoneContent(const std::string& txnId) const {
    return "{\"transaction_id\":\"" + esc(txnId) + "\"}";
}

std::string VerificationManager::buildCancelContent(const std::string& txnId,
    CancelCode code, const std::string& reason) const {
    return "{\"transaction_id\":\"" + esc(txnId) + "\","
           "\"code\":\"" + cancelCodeToString(code) + "\","
           "\"reason\":\"" + esc(reason.empty() ? cancelCodeToString(code) : reason) + "\"}";
}

std::vector<VerificationEmoji> VerificationManager::computeEmojis(
    VerificationTransaction& txn) const {
    if (!txn.sas.valid || !txn.sas.theirKeySet) return {};
    std::string bytes = sasGenerateBytes(txn.sas, "MATRIX_KEY_VERIFICATION_SAS");
    if (bytes.empty()) return {};
    return computeSasEmojis(bytes);
}

bool VerificationManager::verifyTheirMac(VerificationTransaction& txn,
    const std::string& theirMacJson, const std::string& ourDeviceId,
    const std::string& ourEd25519, const std::string& ourCurve25519) const {
    if (!txn.sas.valid) return false;
    std::string infoKey = macInfo(txn.transactionId, ourDeviceId, ourEd25519, ourCurve25519);
    return sasVerifyMac(txn.sas, theirMacJson, ourEd25519, infoKey);
}

std::string VerificationManager::computeCommitment(const std::string& startContentJson,
    const std::string& ourSasPubkey) const {
    // SHA-256 of canonical_start_json + pubkey, then base64-encode
    std::string input = startContentJson + ourSasPubkey;
    // Use libolm's SHA-256 via OlmUtility
    size_t utilSize = olm_utility_size();
    std::vector<uint8_t> utilMem(utilSize);
    OlmUtility* util = olm_utility(utilMem.data());
    size_t hashLen = olm_sha256_length(util);
    std::vector<uint8_t> hash(hashLen);
    size_t ret = olm_sha256(util, input.data(), input.size(), hash.data(), hashLen);
    if (ret == olm_error()) return {};
    // base64 encode
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, vb = -6;
    for (size_t i = 0; i < hashLen; i++) {
        val = (val << 8) + hash[i]; vb += 8;
        while (vb >= 0) { out.push_back(b64[(val>>vb)&0x3F]); vb -= 6; }
    }
    if (vb > -6) out.push_back(b64[((val<<8)>>(vb+8))&0x3F]);
    while (out.size()%4) out.push_back('=');
    return out;
}

std::string VerificationManager::macInfo(const std::string& txnId,
    const std::string& deviceId, const std::string& ed25519,
    const std::string& curve25519) const {
    return "MATRIX_KEY_VERIFICATION_MAC|" + txnId + "|" + deviceId + "|"
           + ed25519 + "|" + curve25519;
}

} // namespace progressive::desktop
