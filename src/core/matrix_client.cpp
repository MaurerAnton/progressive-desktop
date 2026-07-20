// src/core/matrix_client.cpp — implementation.

#include "matrix_client.hpp"
#include "http_client.hpp"

#include <progressive/login_flow.hpp>
#include <progressive/matrix_error.hpp>
#include <progressive/sync_models.hpp>
#include <progressive/well_known.hpp>
#include <progressive/json_parser.hpp>
#include <progressive/content_utils.hpp>

#include <sstream>
#include <chrono>
#include <atomic>
#include <ctime>

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

ApiResult<std::string> MatrixClient::sendMessage(const std::string& roomId,
                                                  const std::string& body,
                                                  const std::string& msgtype) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    // Build a unique transaction ID: timestamp + counter.
    static std::atomic<uint64_t> txnCounter{0};
    uint64_t txn = static_cast<uint64_t>(std::time(nullptr)) * 1000 +
                    (txnCounter.fetch_add(1) % 1000);
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v3/rooms/"
        << roomId << "/send/m.room.message/" << "pd" << txn;

    // Escape body for JSON
    std::string escaped;
    escaped.reserve(body.size() + 8);
    for (char c : body) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    escaped += buf;
                } else {
                    escaped += c;
                }
        }
    }

    std::ostringstream jsonBody;
    jsonBody << R"({"msgtype":")" << msgtype << R"(","body":")" << escaped << R"("})";

    auto resp = httpPost(url.str(), jsonBody.str(), authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = progressive::parseJsonStringValue(resp.body, "event_id");
        r.ok = !r.data.empty();
        if (!r.ok) r.error.message = "send: no event_id in response";
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

ApiResult<std::string> MatrixClient::getMessages(const std::string& roomId,
                                                    const std::string& from,
                                                    int limit) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v3/rooms/"
        << roomId << "/messages?dir=b&limit=" << limit;
    if (!from.empty()) url << "&from=" << from;

    auto resp = httpGet(url.str(), authHeaders(), 30000);
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

ApiResult<bool> MatrixClient::setReadMarker(const std::string& roomId,
                                              const std::string& eventId) {
    ApiResult<bool> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v3/rooms/" << roomId << "/read_markers";
    std::string body = R"({"m.read":")" + eventId + R"("})";
    auto resp = httpPost(url.str(), body, authHeaders(), 10000);
    r.httpStatus = resp.statusCode;
    r.ok = resp.success;
    r.data = resp.success;
    if (!resp.success && !resp.body.empty()) {
        r.error = progressive::parseMatrixErrorJson(resp.body);
    }
    return r;
}

ApiResult<std::string> MatrixClient::createRoom(const std::string& name,
                                                  const std::string& topic,
                                                  bool isDirect,
                                                  const std::vector<std::string>& inviteUserIds) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }

    // Build JSON body
    std::ostringstream o;
    o << R"({"name":")" << name << R"(",)";
    if (!topic.empty()) {
        o << R"("topic":")" << topic << R"(",)";
    }
    o << R"("is_direct":)" << (isDirect ? "true" : "false");
    if (!inviteUserIds.empty()) {
        o << R"(,"invite":[)";
        for (size_t i = 0; i < inviteUserIds.size(); ++i) {
            if (i > 0) o << ",";
            o << R"(")" << inviteUserIds[i] << R"(")";
        }
        o << "]";
    }
    o << R"(,"visibility":"private"})";

    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/createRoom",
                         o.str(), authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = progressive::parseJsonStringValue(resp.body, "room_id");
        r.ok = !r.data.empty();
        if (!r.ok) r.error.message = "createRoom: no room_id in response";
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

ApiResult<std::string> MatrixClient::startDirectMessage(const std::string& userId) {
    // For now: create a new direct room with this user.
    // Room name: the other user's displayname or ID (simplified to their localpart)
    std::string otherName = userId;
    if (otherName[0] == '@') {
        auto colon = otherName.find(':');
        if (colon != std::string::npos) otherName = otherName.substr(1, colon - 1);
        else otherName = otherName.substr(1);
    }
    return createRoom(otherName, "", true, {userId});
}

ApiResult<std::string> MatrixClient::searchUsers(const std::string& query, int limit) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    std::ostringstream o;
    o << R"({"search_term":")" << query << R"(","limit":)" << limit << "}";
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/user_directory/search",
                         o.str(), authHeaders(), 15000);
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

ApiResult<std::string> MatrixClient::getUserProfile(const std::string& userId) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v3/profile/" << userId;
    auto resp = httpGet(url.str(), authHeaders(), 10000);
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

// ---- Helper: generate unique txn ID ----
static std::string genTxnId(const std::string& prefix = "pd") {
    static std::atomic<uint64_t> counter{0};
    uint64_t t = static_cast<uint64_t>(std::time(nullptr)) * 1000 + (counter.fetch_add(1) % 1000);
    return prefix + std::to_string(t);
}

// ---- Helper: JSON escape ----
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

ApiResult<std::string> MatrixClient::joinRoom(const std::string& roomIdOrAlias,
                                                 const std::vector<std::string>& viaServers) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::ostringstream body;
    if (!viaServers.empty()) {
        body << "{\"server_name\":[";
        for (size_t i = 0; i < viaServers.size(); ++i) {
            if (i > 0) body << ",";
            body << "\"" << jsonEscape(viaServers[i]) << "\"";
        }
        body << "]}";
    } else body << "{}";
    // URL-encode the room ID/alias (it may contain # which needs encoding)
    std::string encoded;
    for (char c : roomIdOrAlias) {
        if (c == '#') encoded += "%23";
        else encoded += c;
    }
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/join/" + encoded,
                         body.str(), authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = progressive::parseJsonStringValue(resp.body, "room_id");
        r.ok = !r.data.empty();
        if (!r.ok) r.error.message = "join: no room_id in response";
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
    return r;
}

ApiResult<bool> MatrixClient::leaveRoom(const std::string& roomId) {
    ApiResult<bool> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/leave",
                         "{}", authHeaders(), 15000);
    r.httpStatus = resp.statusCode; r.ok = resp.success; r.data = resp.success;
    if (!resp.success && !resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    return r;
}

ApiResult<std::string> MatrixClient::sendReaction(const std::string& roomId,
                                                    const std::string& eventId,
                                                    const std::string& emoji) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::string txn = genTxnId("react");
    std::ostringstream body;
    body << "{\"m.relates_to\":{\"rel_type\":\"m.annotation\",\"event_id\":\""
         << jsonEscape(eventId) << "\",\"key\":\"" << jsonEscape(emoji) << "\"}}";
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId +
                         "/send/m.reaction/" + txn, body.str(), authHeaders(), 10000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.data = progressive::parseJsonStringValue(resp.body, "event_id");
        r.ok = !r.data.empty();
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    }
    return r;
}

ApiResult<bool> MatrixClient::redactEvent(const std::string& roomId,
                                            const std::string& eventId,
                                            const std::string& reason) {
    ApiResult<bool> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::string txn = genTxnId("redact");
    std::string body = reason.empty() ? "{}"
        : "{\"reason\":\"" + jsonEscape(reason) + "\"}";
    // PUT /_matrix/client/v3/rooms/{roomId}/redact/{eventId}/{txnId}
    auto resp = httpPut(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId +
                        "/redact/" + eventId + "/" + txn, body, authHeaders(), 10000);
    r.httpStatus = resp.statusCode; r.ok = resp.success; r.data = resp.success;
    if (!resp.success && !resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    return r;
}

ApiResult<bool> MatrixClient::pinMessage(const std::string& roomId, const std::string& eventId) {
    // Fetch current pinned events, append the new one, PUT back.
    auto state = getRoomState(roomId);
    if (!state.ok) { ApiResult<bool> r; r.error = state.error; return r; }
    // Find existing pinned list
    std::string pinnedJson;
    auto pos = state.data.find("\"pinned\"");
    if (pos != std::string::npos) {
        auto arrStart = state.data.find('[', pos);
        auto arrEnd = state.data.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            pinnedJson = state.data.substr(arrStart, arrEnd - arrStart + 1);
        }
    }
    // Append eventId
    std::string newPinned;
    if (pinnedJson.empty() || pinnedJson == "[]") {
        newPinned = "[\"" + jsonEscape(eventId) + "\"]";
    } else {
        // Insert before closing ]
        newPinned = pinnedJson.substr(0, pinnedJson.size() - 1) + ",\"" + jsonEscape(eventId) + "\"]";
    }
    std::string body = "{\"pinned\":" + newPinned + "}";
    return sendStateEvent(roomId, "m.room.pinned_events", "", body);
}

ApiResult<bool> MatrixClient::unpinMessage(const std::string& roomId, const std::string& eventId) {
    auto state = getRoomState(roomId);
    if (!state.ok) { ApiResult<bool> r; r.error = state.error; return r; }
    // Find pinned list and remove eventId
    std::string body = "{\"pinned\":[]}";
    auto pos = state.data.find("\"pinned\"");
    if (pos != std::string::npos) {
        auto arrStart = state.data.find('[', pos);
        auto arrEnd = state.data.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arrStr = state.data.substr(arrStart + 1, arrEnd - arrStart - 1);
            // Split by comma, filter out eventId
            std::vector<std::string> ids;
            size_t start = 0;
            while (start < arrStr.size()) {
                auto q1 = arrStr.find('"', start);
                if (q1 == std::string::npos) break;
                auto q2 = arrStr.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                auto id = arrStr.substr(q1 + 1, q2 - q1 - 1);
                if (id != eventId) ids.push_back(id);
                start = q2 + 1;
            }
            std::string newList = "[";
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i > 0) newList += ",";
                newList += "\"" + jsonEscape(ids[i]) + "\"";
            }
            newList += "]";
            body = "{\"pinned\":" + newList + "}";
        }
    }
    return sendStateEvent(roomId, "m.room.pinned_events", "", body);
}

ApiResult<std::string> MatrixClient::getRoomState(const std::string& roomId) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    auto resp = httpGet(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/state",
                        authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) { r.ok = true; r.data = resp.body; }
    else { if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
            r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage; }
    return r;
}

ApiResult<bool> MatrixClient::sendStateEvent(const std::string& roomId,
                                              const std::string& eventType,
                                              const std::string& stateKey,
                                              const std::string& bodyJson) {
    ApiResult<bool> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::string url = account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId +
                      "/state/" + eventType;
    if (!stateKey.empty()) url += "/" + stateKey;
    auto resp = httpPut(url, bodyJson, authHeaders(), 10000);
    r.httpStatus = resp.statusCode; r.ok = resp.success; r.data = resp.success;
    if (!resp.success && !resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    return r;
}

ApiResult<bool> MatrixClient::setRoomTopic(const std::string& roomId, const std::string& topic) {
    std::string body = "{\"topic\":\"" + jsonEscape(topic) + "\"}";
    return sendStateEvent(roomId, "m.room.topic", "", body);
}

ApiResult<bool> MatrixClient::setRoomName(const std::string& roomId, const std::string& name) {
    std::string body = "{\"name\":\"" + jsonEscape(name) + "\"}";
    return sendStateEvent(roomId, "m.room.name", "", body);
}

ApiResult<std::string> MatrixClient::getRoomMembers(const std::string& roomId) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    auto resp = httpGet(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/members",
                        authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) { r.ok = true; r.data = resp.body; }
    else { if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body); }
    return r;
}

ApiResult<bool> MatrixClient::kickUser(const std::string& roomId, const std::string& userId, const std::string& reason) {
    ApiResult<bool> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::string body = "{\"user_id\":\"" + jsonEscape(userId) + "\"";
    if (!reason.empty()) body += ",\"reason\":\"" + jsonEscape(reason) + "\"";
    body += "}";
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/kick",
                         body, authHeaders(), 10000);
    r.httpStatus = resp.statusCode; r.ok = resp.success; r.data = resp.success;
    if (!resp.success && !resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    return r;
}

ApiResult<bool> MatrixClient::banUser(const std::string& roomId, const std::string& userId, const std::string& reason) {
    ApiResult<bool> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::string body = "{\"user_id\":\"" + jsonEscape(userId) + "\"";
    if (!reason.empty()) body += ",\"reason\":\"" + jsonEscape(reason) + "\"";
    body += "}";
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/ban",
                         body, authHeaders(), 10000);
    r.httpStatus = resp.statusCode; r.ok = resp.success; r.data = resp.success;
    if (!resp.success && !resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    return r;
}

ApiResult<bool> MatrixClient::unbanUser(const std::string& roomId, const std::string& userId) {
    ApiResult<bool> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::string body = "{\"user_id\":\"" + jsonEscape(userId) + "\"}";
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/unban",
                         body, authHeaders(), 10000);
    r.httpStatus = resp.statusCode; r.ok = resp.success; r.data = resp.success;
    if (!resp.success && !resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
    return r;
}

ApiResult<bool> MatrixClient::setUserPowerLevel(const std::string& roomId, const std::string& userId, int level) {
    // Fetch current power_levels, modify the user's level, PUT back.
    auto state = getRoomState(roomId);
    if (!state.ok) { ApiResult<bool> r; r.error = state.error; return r; }
    // Find m.room.power_levels event content
    auto plPos = state.data.find("\"m.room.power_levels\"");
    if (plPos == std::string::npos) {
        ApiResult<bool> r; r.error.message = "no m.room.power_levels in state"; return r;
    }
    // Find the content object after it — this is tricky with raw JSON.
    // For simplicity, we build a new power_levels body from scratch using
    // the user's level. This is NOT correct for a real client (we'd lose
    // other users' levels), but works for the common case of promoting/demoting
    // a single user when the caller has fetched the full state first.
    // TODO: properly parse and modify the existing power_levels object.
    std::string body = "{\"users\":{\"@*:" + std::string("server") + "\":0},\"users_default\":0,"
                       "\"events_default\":0,\"state_default\":50,\"ban\":50,\"kick\":50,\"redact\":50}";
    // Actually, let's just use the raw state approach: find the power_levels content
    // and modify the user entry inline.
    auto contentStart = state.data.find('{', plPos);
    if (contentStart == std::string::npos) {
        ApiResult<bool> r; r.error.message = "can't find power_levels content"; return r;
    }
    int depth = 0;
    size_t contentEnd = contentStart;
    for (size_t i = contentStart; i < state.data.size(); ++i) {
        if (state.data[i] == '{') depth++;
        else if (state.data[i] == '}') { depth--; if (depth == 0) { contentEnd = i; break; } }
    }
    std::string plContent = state.data.substr(contentStart, contentEnd - contentStart + 1);
    // Find "users" object inside
    auto usersPos = plContent.find("\"users\"");
    if (usersPos == std::string::npos) {
        ApiResult<bool> r; r.error.message = "no users key in power_levels"; return r;
    }
    // Find the user entry
    std::string userKey = "\"" + userId + "\":" + std::to_string(level);
    auto userPos = plContent.find("\"" + userId + "\"");
    if (userPos != std::string::npos && userPos < plContent.size()) {
        // Replace existing level
        auto colonPos = plContent.find(':', userPos + userId.size() + 2);
        auto valueEnd = colonPos + 1;
        while (valueEnd < plContent.size() && (std::isdigit(static_cast<unsigned char>(plContent[valueEnd])) || plContent[valueEnd] == '-')) valueEnd++;
        plContent.replace(colonPos + 1, valueEnd - colonPos - 1, std::to_string(level));
    } else {
        // Insert new user entry — find end of users object
        auto usersObjStart = plContent.find('{', usersPos);
        int ud = 0;
        size_t usersObjEnd = usersObjStart;
        for (size_t i = usersObjStart; i < plContent.size(); ++i) {
            if (plContent[i] == '{') ud++;
            else if (plContent[i] == '}') { ud--; if (ud == 0) { usersObjEnd = i; break; } }
        }
        // Insert before closing }
        std::string insert = (usersObjEnd - usersObjStart > 1) ? "," : "";
        insert += userKey;
        plContent.insert(usersObjEnd, insert);
    }
    return sendStateEvent(roomId, "m.room.power_levels", "", plContent);
}

ApiResult<std::string> MatrixClient::getThreads(const std::string& roomId, const std::string& from, int limit) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v1/rooms/" << roomId
        << "/threads?limit=" << limit;
    if (!from.empty()) url << "&from=" << from;
    auto resp = httpGet(url.str(), authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) { r.ok = true; r.data = resp.body; }
    else { if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body); }
    return r;
}

ApiResult<std::string> MatrixClient::getThreadReplies(const std::string& roomId, const std::string& rootEventId) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    auto resp = httpGet(account_.homeserverUrl + "/_matrix/client/v3/rooms/" + roomId +
                        "/relations/" + rootEventId + "/m.thread", authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) { r.ok = true; r.data = resp.body; }
    else { if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body); }
    return r;
}

ApiResult<std::string> MatrixClient::searchPublicRooms(const std::string& server, const std::string& query, int limit, const std::string& from) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::ostringstream body;
    body << "{";
    if (!server.empty()) body << "\"server\":\"" << jsonEscape(server) << "\",";
    body << "\"limit\":" << limit;
    if (!from.empty()) body << ",\"from\":\"" << jsonEscape(from) << "\"";
    if (!query.empty()) body << ",\"filter\":{\"generic_search_term\":\"" << jsonEscape(query) << "\"}";
    body << "}";
    auto resp = httpPost(account_.homeserverUrl + "/_matrix/client/v3/publicRooms",
                         body.str(), authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) { r.ok = true; r.data = resp.body; }
    else { if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body); }
    return r;
}

ApiResult<std::string> MatrixClient::getSpaceHierarchy(const std::string& spaceId, int maxDepth) {
    ApiResult<std::string> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v1/rooms/" << spaceId
        << "/hierarchy?max_depth=" << maxDepth;
    auto resp = httpGet(url.str(), authHeaders(), 15000);
    r.httpStatus = resp.statusCode;
    if (resp.success) { r.ok = true; r.data = resp.body; }
    else { if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body); }
    return r;
}

ApiResult<std::vector<uint8_t>> MatrixClient::downloadMedia(const std::string& mxcUrl, int width, int height) {
    ApiResult<std::vector<uint8_t>> r;
    if (!isLoggedIn()) { r.error.message = "not logged in"; return r; }
    // Resolve mxc:// to HTTP URL using progressive::resolveMxcDownloadUrl / resolveMxcThumbnailUrl
    std::string httpUrl;
    if (width > 0 && height > 0) {
        httpUrl = progressive::resolveMxcThumbnailUrl(mxcUrl, account_.homeserverUrl, width, height, "scale");
    } else {
        httpUrl = progressive::resolveMxcDownloadUrl(mxcUrl, account_.homeserverUrl);
    }
    if (httpUrl.empty() || httpUrl == mxcUrl) {
        r.error.message = "invalid mxc URL";
        return r;
    }
    auto resp = httpGet(httpUrl, authHeaders(), 30000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        r.ok = true;
        r.data.assign(resp.body.begin(), resp.body.end());
    } else {
        if (!resp.body.empty()) r.error = progressive::parseMatrixErrorJson(resp.body);
        r.error.message = resp.errorMessage.empty() ? r.error.message : resp.errorMessage;
    }
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

MatrixClient::FastSyncResult MatrixClient::syncFast(const std::string& since,
                                                     int timeoutMs) {
    FastSyncResult r;
    if (!isLoggedIn()) {
        r.error.message = "not logged in";
        return r;
    }
    std::ostringstream url;
    url << account_.homeserverUrl << "/_matrix/client/v3/sync"
        << "?timeout=" << timeoutMs
        << "&full_state=false";
    if (!since.empty()) url << "&since=" << since;

    auto resp = httpGet(url.str(), authHeaders(), timeoutMs + 10000);
    r.httpStatus = resp.statusCode;
    if (resp.success) {
        std::string err;
        r.data = parseSyncResponseFast(std::move(resp.body), err);
        r.ok = err.empty();
        if (!r.ok) r.error.message = std::move(err);
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
    if (acct && !acct->userId.empty() && !acct->accessToken.empty()) {
        account_ = *acct;
        return true;
    }
    return false;
}

} // namespace progressive::desktop
