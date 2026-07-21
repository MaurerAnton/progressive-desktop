// src/core/crypto/megolm_store.cpp — Inbound Megolm session storage.

#include "megolm_store.hpp"

#include <progressive/megolm_decryptor.hpp>
#include <progressive/olm.hpp>

#include <algorithm>
#include <sstream>
#include <string_view>

namespace progressive::desktop {

namespace {
std::string mgExtractStr(std::string_view json, std::string_view key) {
    std::string pat = "\"" + std::string(key) + "\":\"";
    auto pos = json.find(pat);
    if (pos == std::string_view::npos) return {};
    pos += pat.size();
    auto end = json.find('"', pos);
    if (end == std::string_view::npos) return {};
    return std::string(json.substr(pos, end - pos));
}
}

struct SessionParams {
    std::string roomId;
    std::string senderKey;
    std::string sessionId;
    std::string sessionKeyBase64;
};

struct MegolmStore::Impl {
    progressive::MegolmSessionManager mgr;
    std::vector<SessionParams> params;  // for persistence
};

MegolmStore::MegolmStore() : impl_(std::make_unique<Impl>()) {}
MegolmStore::~MegolmStore() = default;

bool MegolmStore::addInboundSession(const std::string& roomId,
                                       const std::string& senderKey,
                                       const std::string& sessionId,
                                       const std::string& sessionKeyBase64) {
    std::lock_guard<std::mutex> lk(mtx_);
    bool ok = impl_->mgr.addSession(roomId, senderKey, sessionId, sessionKeyBase64);
    if (ok) impl_->params.push_back({roomId, senderKey, sessionId, sessionKeyBase64});
    return ok;
}

std::string MegolmStore::decrypt(const std::string& roomId,
                                    const std::string& senderKey,
                                    const std::string& sessionId,
                                    const std::string& ciphertext) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto* sess = impl_->mgr.findSession(roomId, senderKey, sessionId);
    if (!sess) return {};
    return progressive::megolmDecrypt(*sess, ciphertext);
}

bool MegolmStore::hasSession(const std::string& roomId,
                                const std::string& senderKey,
                                const std::string& sessionId) {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->mgr.findSession(roomId, senderKey, sessionId) != nullptr;
}

void MegolmStore::addPending(const PendingEncryptedEvent& evt) {
    std::lock_guard<std::mutex> lk(mtx_);
    pending_.push_back(evt);
}

std::vector<PendingEncryptedEvent> MegolmStore::takePendingForSession(
    const std::string& roomId,
    const std::string& senderKey,
    const std::string& sessionId) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<PendingEncryptedEvent> result;
    auto it = std::remove_if(pending_.begin(), pending_.end(),
        [&](const PendingEncryptedEvent& e) {
            if (e.roomId == roomId && e.senderKey == senderKey && e.sessionId == sessionId) {
                result.push_back(e);
                return true;
            }
            return false;
        });
    pending_.erase(it, pending_.end());
    return result;
}

std::string MegolmStore::pickleAll(const std::string& key) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (impl_->params.empty()) return "[]";
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < impl_->params.size(); ++i) {
        if (i > 0) os << ",";
        os << "{\"r\":\"" << impl_->params[i].roomId << "\""
           << ",\"k\":\"" << impl_->params[i].senderKey << "\""
           << ",\"s\":\"" << impl_->params[i].sessionId << "\""
           << ",\"d\":\"" << impl_->params[i].sessionKeyBase64 << "\"}";
    }
    os << "]";
    std::string raw = os.str();
    // XOR-encrypt with pickleKey (simple obfuscation on disk)
    if (key.empty()) return raw;
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] ^= key[i % key.size()];
    // Hex-encode for safe storage
    std::string hex;
    for (unsigned char c : raw) {
        static const char h[] = "0123456789abcdef";
        hex += h[c >> 4];
        hex += h[c & 15];
    }
    return hex;
}

bool MegolmStore::unpickleAll(const std::string& key, const std::string& data) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (data.empty() || data == "[]") return true;
    // Hex-decode
    std::string raw;
    if (data.size() % 2 == 0) {
        for (size_t i = 0; i < data.size(); i += 2)
            raw += (char)strtol(data.substr(i, 2).c_str(), nullptr, 16);
    } else {
        raw = data;
    }
    // XOR-decrypt
    if (!key.empty()) {
        for (size_t i = 0; i < raw.size(); ++i)
            raw[i] ^= key[i % key.size()];
    }
    // Parse JSON array
    size_t pos = raw.find('{');
    while (pos != std::string::npos) {
        size_t end = raw.find("}}", pos);
        if (end == std::string::npos) end = raw.find('}', raw.find('}', pos + 1) + 1);
        if (end == std::string::npos) break;
        std::string obj = raw.substr(pos, end - pos + 2);
        auto r = mgExtractStr(obj, "r");
        auto k = mgExtractStr(obj, "k");
        auto s = mgExtractStr(obj, "s");
        auto d = mgExtractStr(obj, "d");
        if (!r.empty() && !k.empty() && !s.empty() && !d.empty()) {
            // Dedup: skip if session already exists
            if (!impl_->mgr.findSession(r, k, s)) {
                impl_->mgr.addSession(r, k, s, d);
                impl_->params.push_back({r, k, s, d});
            }
        }
        pos = raw.find('{', end + 2);
    }
    return true;
}

int MegolmStore::sessionCount() {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->mgr.sessionCount();
}

int MegolmStore::pendingCount() {
    std::lock_guard<std::mutex> lk(mtx_);
    return static_cast<int>(pending_.size());
}

} // namespace progressive::desktop
