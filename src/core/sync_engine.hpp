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

#include <atomic>
#include <condition_variable>
#include <functional>
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
    int errors = 0;             // consecutive error count
    int syncs = 0;              // total successful syncs
    std::string lastError;
    SyncEngineState state = SyncEngineState::Stopped;
};

class SyncEngine {
public:
    using SyncCallback = std::function<void(const FastSyncResponse&)>;
    using StateCallback = std::function<void(SyncEngineState, const SyncEngineStats&)>;
    using AuthErrorCallback = std::function<void()>;

    SyncEngine();
    ~SyncEngine();

    void setClient(MatrixClient* c) { client_ = c; }
    void setSessionStore(SessionStore* s) { store_ = s; }
    void onSync(SyncCallback cb) { syncCb_ = std::move(cb); }
    void onStateChange(StateCallback cb) { stateCb_ = std::move(cb); }
    // Called when the access token is invalid (M_UNKNOWN_TOKEN) — UI should
    // clear the saved session and show the login dialog.
    void onAuthError(AuthErrorCallback cb) { authErrCb_ = std::move(cb); }

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

    MatrixClient* client_ = nullptr;
    SessionStore* store_ = nullptr;
    SyncCallback syncCb_;
    StateCallback stateCb_;
    AuthErrorCallback authErrCb_;

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};

    std::string sinceToken_;
    SyncEngineStats stats_;
};

} // namespace progressive::desktop
