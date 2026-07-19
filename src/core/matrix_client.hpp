// src/core/matrix_client.hpp — Matrix Client-Server API client.
//
// Wraps the desktop libcurl HTTP client + the progressive_native parsers
// (login_flow, well_known, matrix_error, sync_models, auth_models).
// The actual JSON parsing is delegated to progressive_native's Tier-A real
// modules — we only build the HTTP request and parse the response.

#pragma once

#include "http_client.hpp"
#include "account_info.hpp"
#include "session_store.hpp"

#include <progressive/auth_models.hpp>
#include <progressive/login_flow.hpp>
#include <progressive/matrix_error.hpp>
#include <progressive/sync_models.hpp>
#include <progressive/well_known.hpp>

#include <string>
#include <unordered_map>

namespace progressive::desktop {

// Result of an API call. Either success (data populated) or error (errcode set).
template <typename T>
struct ApiResult {
    bool ok = false;
    T data{};
    progressive::MatrixError error;          // populated if !ok
    int httpStatus = 0;
};

// Account/session info persisted across runs.
// (Defined in account_info.hpp — shared with SessionStore to avoid circular include.)

class MatrixClient {
public:
    MatrixClient();
    ~MatrixClient();

    // ---- Homeserver discovery ----

    // Resolve a user-entered server name to a homeserver base URL.
    // 1. formatServerUrl (from well_known.cpp)
    // 2. GET /.well-known/matrix/client
    // 3. parseServerDiscovery (from well_known.cpp)
    // 4. fallback to formatted URL if no well-known
    ApiResult<std::string> discoverHomeserver(const std::string& userInput);

    // GET /_matrix/client/versions — minimum sanity check before login.
    ApiResult<std::string> getVersions();

    // GET /_matrix/client/v3/login — list supported login flows.
    // Uses parseLoginFlows() from progressive_native/login_flow.cpp.
    ApiResult<progressive::LoginAuthFlowsResult> getLoginFlows();

    // POST /_matrix/client/v3/login with m.login.password.
    // Uses parseCredentials() (we do it here, response is small JSON).
    ApiResult<AccountInfo> loginWithPassword(const std::string& username,
                                              const std::string& password,
                                              const std::string& deviceId = "");

    // POST /_matrix/client/v3/logout — invalidate the current access token.
    ApiResult<bool> logout();

    // ---- Sync ----

    // GET /_matrix/client/v3/sync — long-poll.
    // Uses parseSyncResponse() from progressive_native/sync_handler.cpp
    // (which delegates to sync_models.cpp's parser).
    ApiResult<progressive::SyncResponse> sync(const std::string& since = "",
                                                int timeoutMs = 30000);

    // ---- Account / session ----

    void setAccount(const AccountInfo& acct);
    const AccountInfo& account() const { return account_; }
    bool isLoggedIn() const { return !account_.accessToken.empty(); }

    void setSessionStore(SessionStore* store) { sessionStore_ = store; }

    // Persist current account to the session store (if set).
    bool persistSession();

    // Load saved account from session store. Returns true if restored.
    bool loadSavedSession();

private:
    AccountInfo account_;
    SessionStore* sessionStore_ = nullptr;

    // Build the standard auth header if logged in.
    std::unordered_map<std::string, std::string> authHeaders() const;
};

} // namespace progressive::desktop
