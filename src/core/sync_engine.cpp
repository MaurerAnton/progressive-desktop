// src/core/sync_engine.cpp

#include "sync_engine.hpp"

#include <chrono>
#include <algorithm>
#include <iostream>

namespace progressive::desktop {

SyncEngine::SyncEngine() = default;

SyncEngine::~SyncEngine() {
    stop();
}

void SyncEngine::start() {
    if (running_.exchange(true)) return;  // already running
    isFirstSync_ = true;  // first sync will use full_state=true to load ALL rooms

    // Load saved since-token if available.
    if (store_) {
        auto tok = store_->loadSyncToken();
        if (tok) sinceToken_ = *tok;
    }

    worker_ = std::thread([this] { run(); });
}

void SyncEngine::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    // Don't join — the worker may be blocked in a 10s HTTP request.
    // Detach so it finishes on its own when running_ is checked next.
    // The worker checks running_ after each sync cycle and exits.
    if (worker_.joinable()) worker_.detach();
}

void SyncEngine::pause() {
    paused_ = true;
    cv_.notify_all();
}

void SyncEngine::resume() {
    paused_ = false;
    cv_.notify_all();
}

void SyncEngine::setState(SyncEngineState s) {
    stats_.state = s;
    if (stateCb_) stateCb_(s, stats_);
}

int SyncEngine::computeBackoffMs(int consecutiveErrors) const {
    // Exponential backoff capped at 60s. 1s, 2s, 4s, 8s, 16s, 32s, 60s.
    int base = 1000 << std::min(consecutiveErrors, 6);
    return std::min(base, 60000);
}

void SyncEngine::run() {
    setState(sinceToken_.empty() ? SyncEngineState::InitialSync
                                  : SyncEngineState::Running);

    while (running_) {
        // Pause gate
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return !paused_ || !running_; });
        }
        if (!running_) break;

        // First sync uses full_state=true — server returns ALL rooms with full
        // state. This can take longer (10MB+ response). Use 30s timeout.
        bool fullState = isFirstSync_.exchange(false) || sinceToken_.empty();
        int timeout = fullState ? 30000 : 10000;
        auto result = client_->syncFast(sinceToken_, timeout, fullState);

        if (!result.ok) {
            stats_.errors++;
            stats_.lastError = result.error.message.empty()
                ? result.error.code
                : result.error.message;

            // Detailed logging for token errors — helps diagnose why sessions
            // expire unexpectedly. Captures: timestamp, error code, HTTP status,
            // error message, since token, our user ID.
            std::fprintf(stderr, "[session] ERROR at %ld: code=%s http=%d msg=%s\n",
                         std::time(nullptr),
                         result.error.code.c_str(),
                         result.httpStatus,
                         result.error.message.c_str());
            std::fprintf(stderr, "[session]   since_token=%s user=%s\n",
                         sinceToken_.substr(0, 20).c_str(),
                         client_ ? client_->account().userId.c_str() : "(null)");

            // Detect invalid access token — notify UI (soft logout).
            if (result.error.code == "M_UNKNOWN_TOKEN") {
                std::fprintf(stderr, "[session] M_UNKNOWN_TOKEN — access token is invalid.\n"
                                     "  Possible causes:\n"
                                     "    1. Token expired (rare — Synapse doesn't expire by default)\n"
                                     "    2. Password was changed\n"
                                     "    3. Logged out from another client with this device_id\n"
                                     "    4. Server-side token cleanup\n"
                                     "    5. SQLite session.db was corrupted and token is garbage\n");
                setState(SyncEngineState::Stopped);
                if (authErrCb_) authErrCb_();
                running_ = false;
                break;
            }

            setState(SyncEngineState::Backoff);

            int backoff = computeBackoffMs(stats_.errors);
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(backoff),
                          [this] { return !running_; });
            continue;
        }

        // Success — update token + stats.
        stats_.errors = 0;
        stats_.syncs++;
        sinceToken_ = std::string(result.data.nextBatch);

        stats_.roomsJoined += static_cast<int>(result.data.joinedRooms.size());
        stats_.invites     += static_cast<int>(result.data.invitedRoomIds.size());
        stats_.timelineEvents += result.data.totalTimelineEvents;
        stats_.toDeviceEvents += result.data.toDeviceEvents;

        // Process to-device events (E2EE): m.room_key adds megolm sessions,
        // m.room.encrypted handles Olm 1:1 (decrypts room_key delivery).
        processToDeviceEvents(result.data);

        // Persist token.
        if (store_ && !sinceToken_.empty()) {
            store_->saveSyncToken(sinceToken_);
        }

        // Emit to UI thread.
        if (syncCb_) syncCb_(result.data);

        setState(SyncEngineState::Running);
    }

    setState(SyncEngineState::Stopped);
}

void SyncEngine::processToDeviceEvents(const FastSyncResponse& resp) {
    for (const auto& evt : resp.toDeviceEventList) {
        if (evt.type == "m.room_key") {
            std::string contentStr(evt.contentJson);
            if (decryptor_.handleRoomKey(contentStr)) {
                stats_.decryptedEvents++;
                std::cerr << "[e2ee] added megolm session (room_key from "
                          << evt.senderId << ")\n";
            } else {
                stats_.decryptErrors++;
                std::cerr << "[e2ee] failed to add room_key from "
                          << evt.senderId << ": " << contentStr << "\n";
            }
        } else if (evt.type == "m.room.encrypted") {
            // Olm 1:1 — decrypts to a m.room_key event (or m.dummy etc.)
            std::string contentStr(evt.contentJson);
            std::string innerPlaintext = decryptor_.handleOlmEncryptedToDevice(
                std::string(evt.senderId), contentStr);
            if (!innerPlaintext.empty()) {
                stats_.decryptedEvents++;
                std::cerr << "[e2ee] decrypted Olm 1:1 to-device from "
                          << evt.senderId << " (" << innerPlaintext.size() << " bytes)\n";
            } else {
                stats_.decryptErrors++;
                std::cerr << "[e2ee] Olm 1:1 decryption failed from "
                          << evt.senderId << "\n";
            }
        }
    }
}

// Upload device keys to the server. Call once at login.
void SyncEngine::uploadDeviceKeys() {
    if (!client_ || !client_->isLoggedIn()) return;
    if (!decryptor_.isInitialized()) return;

    std::string userId = client_->account().userId;
    std::string deviceId = client_->account().deviceId;
    if (deviceId.empty()) deviceId = "PROGRESSIVE_DESKTOP";

    std::cerr << "[e2ee] uploading device keys for " << userId << "/" << deviceId << "\n";
    std::string body = decryptor_.buildKeysUploadBody(userId, deviceId, 10);

    // Upload synchronously — this is fast (small JSON)
    auto result = client_->uploadKeys(body);
    if (result.ok) {
        std::cerr << "[e2ee] device keys uploaded. Server response: "
                  << (result.data.size() > 200 ? result.data.substr(0, 200) + "..." : result.data) << "\n";
    } else {
        std::cerr << "[e2ee] device key upload FAILED: " << result.error.message << "\n";
    }
}

} // namespace progressive::desktop
