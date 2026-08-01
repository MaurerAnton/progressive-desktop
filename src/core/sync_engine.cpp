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
    if (worker_.joinable()) worker_.join();
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
        int timeout = useEmptySince ? 15000 : syncTimeoutMs_;
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
                    LOG(LogChannel::E2EE, "sync /refresh: refreshToken len=%zu",
                        client_->account().refreshToken.size());
                    std::fprintf(stderr, "[session]   trying /refresh with refresh token...\n");
                    auto refresh = client_->refreshAccessToken(client_->account().refreshToken);
                    if (refresh.httpStatus == 200 && !refresh.data.accessToken.empty()) {
                        std::fprintf(stderr, "[session]   /refresh OK — new access token obtained\n");
                        AccountInfo newAcct = client_->account();
                        newAcct.accessToken = refresh.data.accessToken;
                        if (!refresh.data.refreshToken.empty())
                            newAcct.refreshToken = refresh.data.refreshToken;
                        client_->setAccount(newAcct);
                        decryptor_.setCryptoContext(newAcct.userId, newAcct.deviceId,
                                                      newAcct.homeserverUrl, newAcct.accessToken);
                    client_->persistSession();
                    continue;  // retry sync with new token
                    }
                    std::fprintf(stderr, "[session]   /refresh FAILED: %s\n",
                                 refresh.error.message.c_str());
                }

                if (client_ && backupPathProvider_) {
                    std::string backupDir = backupPathProvider_();
                    if (!backupDir.empty()) {
                        std::error_code ec;
                        std::filesystem::create_directories(backupDir, ec);
                        if (!ec) {
                            auto acct = client_->account();
                            std::string filename = acct.userId + "_" +
                                std::to_string(std::time(nullptr)) + ".session";
                            std::ofstream backup(backupDir + filename);
                            if (backup) {
                                backup << "user_id=" << acct.userId << "\n"
                                       << "device_id=" << acct.deviceId << "\n"
                                       << "homeserver=" << acct.homeserverUrl << "\n"
                                       << "refresh_token=" << acct.refreshToken << "\n";
                            }
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
        if (!running_) break;

        if (!result.data.deviceListChanged.empty()) {
            decryptor_.markDevicesStale(result.data.deviceListChanged);
            LOG(LogChannel::E2EE, "device_lists: marked %zu users as stale",
                result.data.deviceListChanged.size());
        }
        if (!running_) break;

        // Persist token.
        if (store_ && !sinceToken_.empty()) {
            store_->saveSyncToken(sinceToken_);
        }

        // Emit to UI thread.
        if (syncCb_) syncCb_(result.data);

        // Update OTK count tracking from sync response
        if (!running_) break;
        if (result.data.signedCurve25519Count > 0) {
            decryptor_.account()->setUploadedKeyCount(result.data.signedCurve25519Count);
        }

        // Auto-upload one-time keys if running low
        if (!running_) break;
        if (result.data.signedCurve25519Count >= 0 && result.data.signedCurve25519Count < 50) {
            LOG(LogChannel::E2EE, "sync: OTK count=%d (<50) — uploading fresh keys",
                result.data.signedCurve25519Count);
            uploadDeviceKeys(true);
        }

        // Auto-upload the fallback key if the server reports none unused.
        // Absence of "signed_curve25519" means our fallback was claimed (or
        // never uploaded) — generate + upload a fresh one.
        if (!running_) break;
        if (decryptor_.accountShared()) {
            std::string userId = client_ ? client_->account().userId : "";
            bool hasUnusedFallback = false;
            for (const auto& type : result.data.unusedFallbackKeyTypes) {
                if (type == "signed_curve25519") { hasUnusedFallback = true; break; }
            }
            auto now = std::chrono::steady_clock::now();
            if (!hasUnusedFallback) {
                if (now - lastFallbackUploadAt_[userId] >= kFallbackUploadCooldown) {
                    LOG(LogChannel::E2EE, "sync: no unused fallback key — uploading");
                    uploadFallbackKey();
                    lastFallbackUploadAt_[userId] = now;
                }
            }
            // Forget old fallback key 5 min after a successful new one was published
            auto pit = lastFallbackPublishedAt_.find(userId);
            if (pit != lastFallbackPublishedAt_.end() && now - pit->second >= kFallbackForgetDelay) {
                decryptor_.account()->forgetOldFallbackKey();
                LOG(LogChannel::E2EE, "sync: forgot old fallback key (published 5 min ago)");
                lastFallbackPublishedAt_.erase(pit);
            }
        }

        setState(SyncEngineState::Running);
    }

    setState(SyncEngineState::Stopped);
}

void SyncEngine::processToDeviceEvents(const FastSyncResponse& resp) {
    LOG(LogChannel::E2EE, "processToDevice: %zu toDevice events", resp.toDeviceEventList.size());
    for (const auto& evt : resp.toDeviceEventList) {
        std::fprintf(stderr, "[E2EE] RAW toDevice type='%s' sender='%s' content='%s'\n",
            std::string(evt.type).c_str(), std::string(evt.senderId).c_str(),
            std::string(evt.contentJson).c_str());
        if (evt.type == "m.room_key") {
            std::string contentStr(evt.contentJson);
            LOG(LogChannel::E2EE, "processToDevice: got m.room_key from=%s content=[%.200s]",
                std::string(evt.senderId).c_str(), contentStr.c_str());
            if (decryptor_.handleRoomKey(contentStr)) {
                LOG(LogChannel::E2EE, "processToDevice: handleRoomKey OK");
                stats_.decryptedEvents++;
                std::cerr << "[e2ee] added megolm session (room_key from "
                          << evt.senderId << ")\n";
            } else {
                LOG(LogChannel::E2EE, "processToDevice: handleRoomKey FAILED");
                stats_.decryptErrors++;
                std::cerr << "[e2ee] failed to add room_key from "
                          << evt.senderId << ": " << contentStr << "\n";
            }
        } else if (evt.type == "m.room.encrypted") {
            LOG(LogChannel::E2EE, "processToDevice: got m.room.encrypted (Olm-wrapped) from=%s",
                std::string(evt.senderId).c_str());
            std::string contentStr(evt.contentJson);
            std::string innerPlaintext = decryptor_.handleOlmEncryptedToDevice(
                std::string(evt.senderId), contentStr);
            std::fprintf(stderr, "[E2EE] Olm result: size=%zu first200='%.200s'\n",
                innerPlaintext.size(), innerPlaintext.empty() ? "(empty)" : innerPlaintext.c_str());
            if (!innerPlaintext.empty()) {
                LOG(LogChannel::E2EE, "processToDevice: Olm decrypt OK — inner type should be m.room_key");
                stats_.decryptedEvents++;
                std::cerr << "[e2ee] decrypted Olm 1:1 to-device from "
                          << evt.senderId << " (" << innerPlaintext.size() << " bytes)\n";
            } else {
                LOG(LogChannel::E2EE, "processToDevice: Olm decrypt FAILED or not m.room_key");
                stats_.decryptErrors++;
                std::cerr << "[e2ee] Olm 1:1 decryption failed from "
                          << evt.senderId << "\n";
            }
        } else if (evt.type.find("m.key.verification.") == 0) {
            LOG(LogChannel::E2EE, "processToDevice: verification event type=%s from=%s",
                std::string(evt.type).c_str(), std::string(evt.senderId).c_str());
            std::string contentStr(evt.contentJson);
            std::string senderStr(evt.senderId);
            std::string userId = client_ ? client_->account().userId : "";
            std::string deviceId = client_ ? client_->account().deviceId : "";
            verificationManager_.handleEvent(
                std::string(evt.type), senderStr, contentStr,
                userId, deviceId, decryptor_.ed25519Key(), decryptor_.curve25519Key());
        }
    }
}

// Upload device keys to the server. Call once at login.
// force=true: bypass otk_uploaded_once flag (used by auto-refresh when count<5).
void SyncEngine::uploadDeviceKeys(bool force) {
    LOG(LogChannel::E2EE, "uploadDeviceKeys: ENTER client=%p isLoggedIn=%d decryptor=%d force=%d",
        (void*)client_.get(),
        client_ ? client_->isLoggedIn() : 0,
        decryptor_.isInitialized() ? 1 : 0,
        force ? 1 : 0);

    if (!client_ || !client_->isLoggedIn()) {
        LOG(LogChannel::E2EE, "uploadDeviceKeys: EXIT — client not ready");
        return;
    }
    if (!decryptor_.isInitialized()) {
        LOG(LogChannel::E2EE, "uploadDeviceKeys: EXIT — decryptor not initialized");
        return;
    }

    // Check if device_keys need uploading (new or recreated account).
    bool needDeviceKeys = !decryptor_.accountShared();

    LOG(LogChannel::E2EE, "uploadDeviceKeys: shared=%d needDeviceKeys=%d",
        decryptor_.accountShared() ? 1 : 0, needDeviceKeys ? 1 : 0);

    if (decryptor_.accountShared() && !force) {
        LOG(LogChannel::E2EE, "uploadDeviceKeys: shared=true, skipping "
            "(OTKs managed by auto-refresh)");
        return;
    }

    std::string userId = client_->account().userId;
    std::string deviceId = client_->account().deviceId;
    if (deviceId.empty()) deviceId = "PROGRESSIVE_DESKTOP";

    int serverCount = decryptor_.account()->uploadedKeyCount();
    int maxKeys = 100;
    int needed = std::max(0, maxKeys - serverCount);
    if (needed == 0 && decryptor_.accountShared() && !needDeviceKeys) {
        LOG(LogChannel::E2EE, "uploadDeviceKeys: OTKs sufficient (count=%d), skipping",
            serverCount);
        return;
    }

    // Always discard old unpublished OTKs before generating new ones.
    // Prevents sequential ID collisions (400 "already exists").
    // DEBT(E2EE): mark_keys_as_published also marks the fallback key published,
    // so a pending-unpublished fallback after a failed uploadFallbackKey is
    // orphaned here. The /sync fallback trigger regenerates it (self-healing).
    decryptor_.markOneTimeKeysPublished();
    LOG(LogChannel::E2EE, "uploadDeviceKeys: discarded old OTKs before generating fresh");

    LOG(LogChannel::E2EE, "uploadDeviceKeys: uploading for %s/%s", userId.c_str(), deviceId.c_str());
    std::string body = decryptor_.buildKeysUploadBody(userId, deviceId, needed, needDeviceKeys, !decryptor_.accountShared());
    LOG(LogChannel::E2EE, "uploadDeviceKeys: our curve25519=%s ed25519=%s",
        decryptor_.curve25519Key().c_str(),
        decryptor_.ed25519Key().c_str());

    auto result = client_->uploadKeys(body);
    LOG(LogChannel::E2EE, "uploadDeviceKeys: result ok=%d httpStatus=%d bodyLen=%zu",
        result.ok ? 1 : 0, result.httpStatus, body.size());

    if (result.ok) {
        LOG(LogChannel::E2EE, "uploadDeviceKeys: SUCCESS — response=[%.200s]", result.data.c_str());
        // Mark OTKs as published — they remain usable by olm_create_inbound_session
        // but won't be returned by olm_account_one_time_keys (prevents re-upload).
        decryptor_.markOneTimeKeysPublished();
        if (needDeviceKeys) {
            decryptor_.markAccountAsShared();
            LOG(LogChannel::E2EE, "uploadDeviceKeys: account marked as shared");
        }
        if (needed > 0) {
            decryptor_.account()->setUploadedKeyCount(serverCount + needed);
            LOG(LogChannel::E2EE, "uploadDeviceKeys: OTK count updated locally to %d (was %d, added %d)",
                serverCount + needed, serverCount, needed);
        }
        {
            std::string queryBody = "{\"device_keys\":{\"" + userId + "\":[]}}";
            auto queryResp = client_->queryKeys(queryBody);
            LOG(LogChannel::E2EE, "uploadDeviceKeys: self-query http=%d body=%.800s",
                queryResp.httpStatus,
                queryResp.ok ? queryResp.data.c_str() : "");
        }
    } else {
        LOG(LogChannel::E2EE, "uploadDeviceKeys: FAILED — error=%s (OTKs persisted, will retry via auto-refresh)",
            result.error.message.c_str());
    }

    // Save pickle regardless of upload success — OTKs are in memory now and must
    // be persisted for createInbound to work on restart. (fixes bug #12: 401 race
    // prevented otk_persisted from being set, causing infinite regeneration every
    // restart and eventual OTK eviction from libolm's MAX-100 bounded list)
    {
        std::string pickleKey = userId + "/" + deviceId;
        std::string newPickle = decryptor_.saveAccountPickle(pickleKey);
        if (!newPickle.empty() && store_) {
            store_->saveOlmAccount(newPickle, pickleKey, decryptor_.accountShared(),
                                    decryptor_.account()->uploadedKeyCount());
            LOG(LogChannel::E2EE, "uploadDeviceKeys: account pickle saved (shared=%d, published=%d)",
                decryptor_.accountShared() ? 1 : 0, result.ok ? 1 : 0);
        }
    }
}

void SyncEngine::uploadFallbackKey() {
    LOG(LogChannel::E2EE, "uploadFallbackKey: ENTER client=%p isLoggedIn=%d decryptor=%d",
        client_.get(), client_ && client_->isLoggedIn() ? 1 : 0,
        decryptor_.isInitialized() ? 1 : 0);

    if (!client_ || !client_->isLoggedIn()) return;
    if (!decryptor_.isInitialized()) return;
    if (!decryptor_.accountShared()) return;

    std::string userId = client_->account().userId;
    std::string deviceId = client_->account().deviceId;
    if (deviceId.empty()) deviceId = "PROGRESSIVE_DESKTOP";

    // Generate a fresh fallback key only if none is unpublished yet —
    // keeps retries idempotent after a failed upload.
    if (decryptor_.account()->unpublishedFallbackKey().empty()) {
        if (!decryptor_.account()->generateFallbackKey()) {
            LOG(LogChannel::E2EE, "uploadFallbackKey: generateFallbackKey FAILED");
            return;
        }
        LOG(LogChannel::E2EE, "uploadFallbackKey: generated fresh fallback key");
    }

    std::string fallbackSection = decryptor_.buildFallbackKeysSection(userId, deviceId);
    if (fallbackSection.empty()) {
        LOG(LogChannel::E2EE, "uploadFallbackKey: no fallback key to upload (section empty)");
        return;
    }

    std::string body = "{\"fallback_keys\":" + fallbackSection + "}";
    auto result = client_->uploadKeys(body);
    LOG(LogChannel::E2EE, "uploadFallbackKey: upload ok=%d httpStatus=%d",
        result.ok ? 1 : 0, result.httpStatus);

    if (result.ok) {
        // markOneTimeKeysPublished marks the fallback key as published too
        // (libolm: mark_keys_as_published covers both OTKs and fallback).
        // DEBT(E2EE): this also marks any unpublished OTKs as published without
        // ever uploading them, orphaning them. The OTK auto-refresh at uploadDeviceKeys
        // discards old unpublished OTKs then regenerates (self-healing).
        decryptor_.markOneTimeKeysPublished();
        LOG(LogChannel::E2EE, "uploadFallbackKey: SUCCESS — fallback published");
        std::string ufUserId = client_ ? client_->account().userId : "";
        if (!ufUserId.empty()) lastFallbackPublishedAt_[ufUserId] = std::chrono::steady_clock::now();
        std::string pickleKey = userId + "/" + deviceId;
        std::string newPickle = decryptor_.saveAccountPickle(pickleKey);
        if (!newPickle.empty() && store_) {
            store_->saveOlmAccount(newPickle, pickleKey, decryptor_.accountShared(),
                                    decryptor_.account()->uploadedKeyCount());
            LOG(LogChannel::E2EE, "uploadFallbackKey: account pickle saved");
        }
    } else {
        // Fallback key stays unpublished — cooldown (60s) prevents hammering,
        // next sync retries with the same key.
        LOG(LogChannel::E2EE, "uploadFallbackKey: FAILED — retry after cooldown, error=%s",
            result.error.message.c_str());
    }
}

} // namespace progressive::desktop
