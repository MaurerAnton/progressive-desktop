// src/core/session_store.cpp

#include "session_store.hpp"

#include <sqlite3.h>
#include <cstring>
#include <sstream>

namespace progressive::desktop {

SessionStore::SessionStore() = default;

SessionStore::~SessionStore() {
    close();
}

bool SessionStore::open(const std::string& dbPath) {
    if (db_) close();
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        // db_ may still be non-null on failure — free it.
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }
    // Durability recipe — same as agora-desktop.
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=FULL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    if (!createSchema()) {
        close();
        return false;
    }
    return true;
}

void SessionStore::close() {
    if (db_) {
        checkpoint();
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SessionStore::createSchema() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS account ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"  // singleton row
        "  user_id TEXT NOT NULL,"
        "  device_id TEXT,"
        "  homeserver_url TEXT NOT NULL,"
        "  access_token TEXT NOT NULL,"
        "  refresh_token TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS sync_state ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  since_token TEXT"
        ");";
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        return false;
    }
    return true;
}

void SessionStore::checkpoint() {
    if (!db_) return;
    sqlite3_wal_checkpoint_v2(db_, "main", SQLITE_CHECKPOINT_TRUNCATE,
                              nullptr, nullptr);
}

bool SessionStore::saveAccount(const AccountInfo& a) {
    if (!db_) return false;
    const char* sql =
        "INSERT INTO account (id, user_id, device_id, homeserver_url, access_token, refresh_token) "
        "VALUES (1, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  user_id=excluded.user_id, device_id=excluded.device_id, "
        "  homeserver_url=excluded.homeserver_url, access_token=excluded.access_token, "
        "  refresh_token=excluded.refresh_token;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, a.userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, a.deviceId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, a.homeserverUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, a.accessToken.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, a.refreshToken.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;
    checkpoint();
    return true;
}

std::optional<AccountInfo> SessionStore::loadAccount() {
    if (!db_) return std::nullopt;
    const char* sql = "SELECT user_id, device_id, homeserver_url, access_token, refresh_token FROM account WHERE id=1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    std::optional<AccountInfo> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        AccountInfo a;
        a.userId        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.deviceId      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.homeserverUrl = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        a.accessToken   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            a.refreshToken = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        }
        result = std::move(a);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool SessionStore::clearAccount() {
    if (!db_) return false;
    int rc = sqlite3_exec(db_, "DELETE FROM account WHERE id=1;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;
    checkpoint();
    return true;
}

bool SessionStore::saveSyncToken(const std::string& token) {
    if (!db_) return false;
    const char* sql =
        "INSERT INTO sync_state (id, since_token) VALUES (1, ?) "
        "ON CONFLICT(id) DO UPDATE SET since_token=excluded.since_token;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false;
    checkpoint();
    return true;
}

std::optional<std::string> SessionStore::loadSyncToken() {
    if (!db_) return std::nullopt;
    const char* sql = "SELECT since_token FROM sync_state WHERE id=1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        result = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool SessionStore::clearSyncToken() {
    if (!db_) return false;
    int rc = sqlite3_exec(db_, "DELETE FROM sync_state WHERE id=1;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;
    checkpoint();
    return true;
}

} // namespace progressive::desktop
