// src/core/session_store.hpp — SQLite-backed persistence for account + sync token.
//
// Agora-desktop's durability recipe: PRAGMA synchronous=FULL, WAL mode,
// explicit checkpoint after every save. Same approach here.

#pragma once

#include "account_info.hpp"

#include <string>
#include <vector>
#include <optional>
#include <mutex>

struct sqlite3;

namespace progressive::desktop {

struct OlmAccountRecord {
    std::string pickle;
    std::string pickleKey;
    bool shared = false;
    int uploadedKeyCount = 0;
};

class SessionStore {
public:
    SessionStore();
    ~SessionStore();

    // Open/create the database at the given path. Returns false on failure.
    bool open(const std::string& dbPath);

    // Close the database. Safe to call multiple times.
    void close();

    bool isOpen() const { return db_ != nullptr; }

    // ---- Account ----

    bool saveAccount(const AccountInfo& acct);
    std::optional<AccountInfo> loadAccount();
    bool clearAccount(const std::string& userId);

    // List all saved accounts (for switcher UI)
    std::vector<AccountInfo> listAccounts();

    // ---- Sync token ----

    bool saveSyncToken(const std::string& token);
    std::optional<std::string> loadSyncToken();
    bool clearSyncToken();

    // ---- Olm account (E2EE) ----
    bool saveOlmAccount(const std::string& pickle, const std::string& pickleKey,
                         bool shared, int uploadedKeyCount);
    std::optional<OlmAccountRecord> loadOlmAccount(const std::string& pickleKey);

    // ---- Megolm sessions (E2EE) ----
    bool saveMegolmSessions(const std::string& data, const std::string& pickleKey);
    std::optional<std::string> loadMegolmSessions(const std::string& pickleKey);

    // ---- Olm 1:1 sessions (E2EE) ----
    bool saveOlmSessions(const std::string& data, const std::string& pickleKey);
    std::optional<std::string> loadOlmSessions(const std::string& pickleKey);

    // ---- Outbound Megolm sessions (E2EE) ----
    bool saveOutboundSessions(const std::string& data, const std::string& pickleKey);
    std::optional<std::string> loadOutboundSessions(const std::string& pickleKey);

    // ---- E2EE flags ----
    bool saveE2eeFlag(const std::string& key, bool value);
    std::optional<bool> loadE2eeFlag(const std::string& key);

    // ---- Cross-signing keys (MSC1756) ----
    bool saveCrossSigningKeys(const std::string& userId, const std::string& json);
    std::optional<std::string> loadCrossSigningKeys(const std::string& userId);
    // The user-signing pubkey from the stored cross-signing keys ("" if none) —
    // used by the trust computation for the cross-user USK check.
    std::string loadUserSigningPub(const std::string& userId);

    // ---- Verified devices (SAS-verified for key-sharing policy) ----
    bool saveVerifiedDevice(const std::string& userId, const std::string& deviceId);
    // Drop all SAS-verified device records (used after a cross-signing reset —
    // old verifications are meaningless against the new keys).
    bool clearVerifiedDevices();
    bool isDeviceVerified(const std::string& userId, const std::string& deviceId);

    // ---- Hidden rooms ----
    bool saveHiddenRoom(const std::string& roomId);
    bool removeHiddenRoom(const std::string& roomId);
    std::vector<std::string> loadHiddenRooms();

    // Force a WAL checkpoint. Called after each save.
    void checkpoint();

private:
    sqlite3* db_ = nullptr;
    bool createSchema();

private:
    // Serializes all db_ access — the store is now touched from the sync
    // thread AND the UI (device-shield queries on a worker thread).
    // Recursive: open() -> createSchema(), loadUserSigningPub() ->
    // loadCrossSigningKeys() re-enter the lock on the same thread.
    mutable std::recursive_mutex mtx_;
};

} // namespace progressive::desktop
