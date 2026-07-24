// src/core/sync_engine.cpp

#include "sync_engine.hpp"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ctime>
#include "core/debug_log.hpp"

namespace progressive::desktop {

SyncEngine::SyncEngine() = default;

SyncEngine::~SyncEngine() {
    stop();
}

void SyncEngine::start() {
    LOG(LogChannel::DBG, "sync start called");
    if (running_.exchange(true)) return;  // already running

    // Load saved since-token if available (for incremental sync after first run).
    if (store_) {
        auto tok = store_->loadSyncToken();
        if (tok) sinceToken_ = *tok;
    }
    firstRun_ = true;  // next sync uses empty since → gets current state for all rooms

    worker_ = std::thread([this] { run(); });
}

void SyncEngine::stop() {
    // Always set running_ and detach the worker thread.
    // Can't early-return on !exchange→false because authErrCb_ already
    // sets running_=false — if we return here, worker_ stays joinable
    // and ~thread() calls std::terminate().
    running_ = false;
    cv_.notify_all();
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

    int tokenFailures = 0;

    while (running_) {
        // Pause gate
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return !paused_ || !running_; });
        }
        if (!running_) break;

        // First sync after start(): use empty since token even if we have a saved
        // one. This tells the server "give me current state for all rooms" WITHOUT
        // the massive overhead of full_state=true (which sends ALL historical state
        // events). With empty since + full_state=false, the server returns one copy
        // of each current state event — enough for room names, avatars, encryption.
        // Subsequent syncs use the real sinceToken_ for efficient incremental sync.
        // Timeout: 15s for initial sync (more data), 10s for incremental.
        bool useEmptySince = firstRun_;
        std::string since = useEmptySince ? "" : sinceToken_;
        int timeout = useEmptySince ? 15000 : 10000;
        auto result = client_->syncFast(since, timeout, false);

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

            // Detect invalid access token.
            if (result.error.code == "M_UNKNOWN_TOKEN") {
                tokenFailures++;
                LOG(LogChannel::DBG, "M_UNKNOWN_TOKEN — attempt %d/3", tokenFailures);
                if (tokenFailures >= 3) {
                    LOG(LogChannel::DBG, "M_UNKNOWN_TOKEN repeated %d times — forcing auth error",
                        tokenFailures);
                    setState(SyncEngineState::Stopped);
                    LOG(LogChannel::DBG, "calling authErrCb_ (token loop guard)");
                    if (authErrCb_) authErrCb_();
                    running_ = false;
                    break;
                }

                std::fprintf(stderr, "[session] M_UNKNOWN_TOKEN — access token is invalid.\n"
                                     "  Possible causes:\n"
                                     "    1. Token expired (rare — Synapse doesn't expire by default)\n"
                                     "    2. Password was changed\n"
                                     "    3. Logged out from another client with this device_id\n"
                                     "    4. Server-side token cleanup\n"
                                     "    5. SQLite session.db was corrupted and token is garbage\n");

                auto acct = client_->account();
                std::fprintf(stderr, "[session]   user=%s device=%s refresh=%s\n",
                             acct.userId.c_str(),
                             acct.deviceId.c_str(),
                             acct.refreshToken.empty() ? "(none)"
                                 : (acct.refreshToken.substr(0, 8) + "...").c_str());

                // Retry once — may be a transient network error
                std::fprintf(stderr, "[session]   retrying sync once (transient check)...\n");
                auto retry = client_->syncFast(since, timeout, false);
                if (retry.ok) {
                    std::fprintf(stderr, "[session]   retry OK — false alarm, continuing\n");
                    sinceToken_ = std::string(retry.data.nextBatch);
                    stats_.errors = 0;
                    stats_.syncs++;
                    if (syncCb_) syncCb_(retry.data);
                    continue;
                }

                // Try refresh token if available
                if (client_ && !client_->account().refreshToken.empty()) {
                    std::fprintf(stderr, "[session]   trying /refresh with refresh token...\n");
                    auto refresh = client_->refreshAccessToken(client_->account().refreshToken);
                    if (refresh.ok && !refresh.data.accessToken.empty()) {
                        std::fprintf(stderr, "[session]   /refresh OK — new access token obtained\n");
                        AccountInfo newAcct = client_->account();
                        newAcct.accessToken = refresh.data.accessToken;
                        newAcct.refreshToken = refresh.data.refreshToken;
                        client_->setAccount(newAcct);
                    client_->persistSession();
                    continue;  // retry sync with new token
                    }
                    std::fprintf(stderr, "[session]   /refresh FAILED: %s\n",
                                 refresh.error.message.c_str());
                }

                if (client_) {
                    const char* dataHome = getenv("XDG_DATA_HOME");
                    if (!dataHome || !dataHome[0]) {
                        const char* home = getenv("HOME");
                        static std::string homeData;
                        if (home) { homeData = std::string(home) + "/.local/share"; dataHome = homeData.c_str(); }
                    }
                    if (dataHome) {
                        std::string backupDir = std::string(dataHome) + "/progressive-desktop/sessions_backup/";
                        std::filesystem::create_directories(backupDir);
                        auto acct = client_->account();
                        std::string filename = acct.userId + "_" + std::to_string(std::time(nullptr)) + ".session";
                        std::ofstream backup(backupDir + filename);
                        if (backup) {
                            backup << "user_id=" << acct.userId << "\n"
                                   << "device_id=" << acct.deviceId << "\n"
                                   << "homeserver=" << acct.homeserverUrl << "\n"
                                   << "refresh_token=" << acct.refreshToken << "\n";
                        }
                    }
                }

                setState(SyncEngineState::Stopped);
                LOG(LogChannel::DBG, "calling authErrCb_ (fallback after /refresh fail)");
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
        firstRun_ = false;  // only clear on SUCCESS — retries must still use empty since
        tokenFailures = 0;
        stats_.errors = 0;
        stats_.syncs++;
        sinceToken_ = std::string(result.data.nextBatch);

        stats_.roomsJoined += static_cast<int>(result.data.joinedRooms.size());
        stats_.invites     += static_cast<int>(result.data.invitedRooms.size());
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
        if (store_) store_->saveE2eeFlag("keys_published", true);
    } else {
        std::cerr << "[e2ee] device key upload FAILED: " << result.error.message << "\n";
    }
}

} // namespace progressive::desktop
