// src/core/sync_engine.hpp — background /sync loop with backoff.
//
// Runs on a dedicated worker thread. Calls MatrixClient::syncFast() repeatedly
// (simdjson-based zero-copy parse, 50-200x faster than progressive_native's
// hand-rolled parser), emits signals when new events arrive or state changes.
// Exponential backoff on error. Persists the since-token after each successful sync.

#pragma once

#include "matrix_client.hpp"
#include "session_store.hpp"
#include "fast_sync.hpp"
#include "crypto/decryptor.hpp"
#include "crypto/verification.hpp"
#include <functional>
#include <string>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace progressive::desktop {

enum class SyncEngineState {
    Stopped,
    InitialSync,        // first sync, no since-token
    Running,            // incremental sync loop
    Backoff,            // sleeping after error
    Paused,             // user paused
};

struct SyncEngineStats {
    int roomsJoined = 0;        // total joined rooms seen so far
    int invites = 0;
    int timelineEvents = 0;     // cumulative timeline events received
    int toDeviceEvents = 0;
    int decryptedEvents = 0;   // E2EE events successfully decrypted
    int decryptErrors = 0;     // E2EE events that failed to decrypt
    int errors = 0;             // consecutive error count
    int syncs = 0;              // total successful syncs
    std::string lastError;
    SyncEngineState state = SyncEngineState::Stopped;
};

class SyncEngine {
public:
    using SyncCallback = std::function<void(FastSyncResponse)>;
    using StateCallback = std::function<void(SyncEngineState, const SyncEngineStats&)>;
    using AuthErrorCallback = std::function<void()>;

    SyncEngine();
    ~SyncEngine();

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }
    void setSessionStore(std::shared_ptr<SessionStore> s) {
        store_ = std::move(s);
        decryptor_.setVerifiedDeviceChecker([this](const std::string& userId,
            const std::string& deviceId) {
            return store_ ? store_->isDeviceVerified(userId, deviceId) : false;
        });
    }
    void onSync(SyncCallback cb) { syncCb_ = std::move(cb); }
    void onStateChange(StateCallback cb) { stateCb_ = std::move(cb); }
    // Called when the access token is invalid (M_UNKNOWN_TOKEN) — UI should
    // clear the saved session and show the login dialog.
    void onAuthError(AuthErrorCallback cb) { authErrCb_ = std::move(cb); }

    // Access the E2EE decryptor (for setup at login time).
    Decryptor* decryptor() { return &decryptor_; }
    VerificationManager& verificationManager() { return verificationManager_; }
    void setPollTimeout(int ms) { syncTimeoutMs_ = ms; }
    void setBackupPathProvider(std::function<std::string()> provider) {
        backupPathProvider_ = std::move(provider);
    }

    // Upload device keys + one-time keys to the server.
    // Call once after init() + login. Non-blocking (spawns a thread).
    void uploadDeviceKeys(bool force = false);

    // Generate (if needed) + upload the fallback key. Called from the sync
    // loop when the server reports no unused fallback key of our type.
    void uploadFallbackKey();

    // Generate MSK/USK/SSK, upload the three m.cross_signing.* account_data
    // types, and persist the private keys. No-op if already set up.
    bool setupCrossSigning();

    const SyncEngineStats& stats() const { return stats_; }

    // Start the loop. If a saved since-token exists, continues incremental;
    // otherwise does an initial sync.
    void start();

    // Stop the loop (waits for the in-flight request to finish).
    void stop();

    // Pause / resume without losing the since-token.
    void pause();
    void resume();

private:
    void run();
    void setState(SyncEngineState s);
    int computeBackoffMs(int consecutiveErrors) const;
    // Process to-device events from a sync response — handles m.room_key
    // (adds megolm inbound sessions) and m.room.encrypted (Olm 1:1, future).
    void processToDeviceEvents(const FastSyncResponse& resp);

    std::shared_ptr<MatrixClient> client_;
    std::shared_ptr<SessionStore> store_;
    SyncCallback syncCb_;
    StateCallback stateCb_;
    AuthErrorCallback authErrCb_;

    Decryptor decryptor_;
    VerificationManager verificationManager_;

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};

    std::string sinceToken_;
    SyncEngineStats stats_;
    bool firstRun_ = false;  // true → next sync uses empty since (gets current state)
    int syncTimeoutMs_ = 3000;
    std::function<std::string()> backupPathProvider_;
    // Cooldown for fallback re-uploads: servers that never acknowledge the
    // fallback type would otherwise trigger an upload every sync.
    std::map<std::string, std::chrono::steady_clock::time_point> lastFallbackUploadAt_;
    std::map<std::string, std::chrono::steady_clock::time_point> lastFallbackPublishedAt_;
    std::map<std::string, int> fallbackBackoffSecs_;
    static constexpr std::chrono::seconds kFallbackForgetDelay{300};
};

} // namespace progressive::desktop
