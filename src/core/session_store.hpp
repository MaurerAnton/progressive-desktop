// src/core/session_store.hpp — SQLite-backed persistence for account + sync token.
//
// Agora-desktop's durability recipe: PRAGMA synchronous=FULL, WAL mode,
// explicit checkpoint after every save. Same approach here.

#pragma once

#include "account_info.hpp"

#include <string>
#include <optional>

struct sqlite3;

namespace progressive::desktop {

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
    bool clearAccount();

    // ---- Sync token ----

    bool saveSyncToken(const std::string& token);
    std::optional<std::string> loadSyncToken();
    bool clearSyncToken();

    // Force a WAL checkpoint. Called after each save.
    void checkpoint();

private:
    sqlite3* db_ = nullptr;
    bool createSchema();
};

} // namespace progressive::desktop
