// src/core/matrix_client.cpp — implementation.

#include "matrix_client.hpp"
#include "http_client.hpp"

#include <progressive/login_flow.hpp>
#include <progressive/matrix_error.hpp>
#include <progressive/sync_models.hpp>
#include <progressive/well_known.hpp>
#include <progressive/json_parser.hpp>

#include <sstream>
#include <chrono>

namespace progressive::desktop {

namespace {

// Build a JSON body for m.login.password login.
std::string buildLoginBody(const std::string& username,
                           const std::string& password,
                           const std::string& deviceId) {
    std::ostringstream o;
    o << R"({"type":"m.login.password","identifier":{"type":"m.id.user","user":")"
      << username << R"("},"password":")" << password << R"(")";
    if (!deviceId.empty()) {
        o << R"(,"device_id":")" << deviceId << R"(")";
    }
    // Request a refresh token (MSC2918) — server may ignore.
    o << R"(,"refresh_token":true})";
    return o.str();
}

// Parse POST /login response → AccountInfo.
// Uses progressive::parseJsonStringValue for the top-level fields.
// (parseCredentials isn't a single public function in progressive_native;
//  the JSON is simple enough to read directly.)
AccountInfo parseLoginResponse(const std::string& json,
                                const std::string& homeserverUrl) {
    AccountInfo a;
    a.userId        = progressive::parseJsonStringValue(json, "user_id");
    a.accessToken   = progressive::parseJsonStringValue(json, "access_token");
    a.refreshToken  = progressive::parseJsonStringValue(json, "refresh_token");
    a.deviceId      = progressive::parseJsonStringValue(json, "device_id");
    a.homeserverUrl = homeserverUrl;
    return a;
}

} // namespace

MatrixClient::MatrixClient() {
    httpInit();
}

MatrixClient::~MatrixClient() {
    // httpCleanup is global — defer to application shutdown, not per-client.
}

std::unordered_map<std::string, std::string> MatrixClient::authHeaders() const {
    std::unordered_map<std::string, std::string> h;
    if (!account_.accessToken.empty()) {
        h["Authorization"] = "Bearer " + account_.accessToken;
    }
    h["Content-Type"] = "application/json";
    h["Accept"] = "application/json";
    return h;
}

ApiResult<std::string> MatrixClient::discoverHomeserver(const std::string& userInput) {
    ApiResult<std::string> r;
    // Step 1: format the user input as a URL.
    std::string url = progressive::formatServerUrl(userInput);

    // Step 2: try .well-known
    auto resp = httpGet(url + "/.well-known/matrix/client",
                        {{"Accept", "application/json"}}, 10000);
    if (resp.success) {
        auto d = progressive::parseServerDiscovery(resp.body);
        if (d.isValid && !d.homeserverBaseUrl.empty()) {
            r.ok = true;
            r.data = d.homeserverBaseUrl;
            r.httpStatus = resp.statusCode;
            return r;
        }
    }
    // Step 3: fall back to the formatted URL (no well-known).
    // Validate it later via getVersions().
    r.ok = true;
    r.data = url;
    r.httpStatus = resp.statusCode;
    return r;
}

ApiResult<std::string> MatrixClient::getVersions() {
    ApiResult<std::string> r;
    if (account_.homeserverUrl.empty()) {
        r.error.message = "no homeserver URL set";
        return r;
    }
    auto resp = httpGet(account_.homeserverUrl + "/_matrix/client/versions",
                        authHeaders(), 10000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.ok = true;
        r.data = resp.body;
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

ApiResult<progressive::LoginAuthFlowsResult> MatrixClient::getLoginFlows() {
    ApiResult<progressive::LoginAuthFlowsResult> r;
    if (account_.homeserverUrl.empty()) {
        r.error.message = "no homeserver URL set";
        return r;
    }
    auto resp = httpGet(account_.homeserverUrl + "/_matrix/client/v3/login",
                        authHeaders(), 10000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = progressive::parseLoginFlows(resp.body);
        r.ok = true;
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

ApiResult<AccountInfo> MatrixClient::loginWithPassword(const std::string& username,
                                                        const std::string& password,
                                                        const std::string& deviceId) {
    ApiResult<AccountInfo> r;
    if (account_.homeserverUrl.empty()) {
        r.error.message = "no homeserver URL set (call discoverHomeserver first)";
        return r;
    }
    std::string body = buildLoginBody(username, password, deviceId);
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/login",
                         body, authHeaders(), 30000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = parseLoginResponse(resp.body, account_.homeserverUrl);
        if (r.data.isValid() ||
            (!r.data.userId.empty() && !r.data.accessToken.empty())) {
            account_ = r.data;
            r.ok = true;
        } else {
            r.error.code = progressive::ErrorCode::M_UNKNOWN;
            r.error.message = "login response missing user_id or access_token";
        }
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

ApiResult<bool> MatrixClient::logout() {
    ApiResult<bool> r;
    if (!isLoggedIn()) {
        r.ok = true;
        r.data = true;
        return r;
    }
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/logout",
                         "{}", authHeaders(), 10000);
    r.httpStatus = resp.statusCode;
    r.ok = resp.success;
    r.data = resp.success;
    if (!resp.success && !resp.body.empty()) {
        r.error = progressive::parseMatrixErrorJson(resp.body);
    }
    // Clear local state regardless of server response.
    account_ = AccountInfo{};
    return r;
}

ApiResult<progressive::SyncResponse> MatrixClient::sync(const std::string& since,
                                                        int timeoutMs) {
    ApiResult<progressive::SyncResponse> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v3/sync"
        << "?timeout=" << timeoutMs
        << "&full_state=false";
    if (!since.empty()) url << "&since=" << since;

    // The long-poll timeout in the URL is server-side; the HTTP timeout
    // must be slightly longer so we get the response.
    auto resp = httpGet(url.str(), authHeaders(), timeoutMs + 10000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = progressive::parseSyncResponse(resp.body);
        r.ok = true;
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

void MatrixClient::setAccount(const AccountInfo& acct) {
    account_ = acct;
}

bool MatrixClient::persistSession() {
    if (!sessionStore_) return false;
    return sessionStore_->saveAccount(account_);
}

bool MatrixClient::loadSavedSession() {
    if (!sessionStore_) return false;
    auto acct = sessionStore_->loadAccount();
    if (!acct.userId.empty() && !acct.accessToken.empty()) {
        account_ = acct;
        return true;
    }
    return false;
}

} // namespace progressive::desktop
