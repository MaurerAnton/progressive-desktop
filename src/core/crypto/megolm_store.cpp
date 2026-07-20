// src/core/crypto/megolm_store.cpp — Inbound Megolm session storage.

#include "megolm_store.hpp"

#include <progressive/megolm_decryptor.hpp>
#include <progressive/olm.hpp>

#include <algorithm>

namespace progressive::desktop {

struct MegolmStore::Impl {
    progressive::MegolmSessionManager mgr;
};

MegolmStore::MegolmStore() : impl_(std::make_unique<Impl>()) {}
MegolmStore::~MegolmStore() = default;

bool MegolmStore::addInboundSession(const std::string& roomId,
                                       const std::string& senderKey,
                                       const std::string& sessionId,
                                       const std::string& sessionKeyBase64) {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->mgr.addSession(roomId, senderKey, sessionId, sessionKeyBase64);
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
    // For now, we don't persist megolm sessions — they're rebuilt from
    // m.room_key to-device events on each launch. Persistence will be added
    // once the API surface stabilizes.
    (void)key;
    return {};
}

bool MegolmStore::unpickleAll(const std::string& key, const std::string& data) {
    (void)key;
    (void)data;
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
