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
#include "fast_sync.hpp"

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

    // ---- Send message ----

    // POST /_matrix/client/v3/rooms/{roomId}/send/m.room.message/{txnId}
    // Body: {"msgtype":"m.text","body":"..."} (or "m.emote", "m.notice")
    // Returns the event_id of the new event (or error).
    ApiResult<std::string> sendMessage(const std::string& roomId,
                                        const std::string& body,
                                        const std::string& msgtype = "m.text");

    // ---- Paginate timeline ----

    // GET /_matrix/client/v3/rooms/{roomId}/messages?dir=b&from=...&limit=...
    // Returns raw JSON (chunk of events + end token). The caller parses with
    // progressive::parseSyncResponse or similar — kept simple for Phase 2.
    // Returns: { "chunk": [...], "end": "..." } as raw JSON string.
    ApiResult<std::string> getMessages(const std::string& roomId,
                                        const std::string& from = "",
                                        int limit = 30);

    // ---- Read marker ----

    // POST /_matrix/client/v3/rooms/{roomId}/read_markers
    // Body: {"m.read": eventId}
    ApiResult<bool> setReadMarker(const std::string& roomId,
                                  const std::string& eventId);

    // ---- Create room ----

    // POST /_matrix/client/v3/createRoom
    // Creates a new room. If inviteUserIds is non-empty, invites them.
    // If isDirect=true, marks as a DM (m.direct account data).
    // Returns the new room_id.
    ApiResult<std::string> createRoom(const std::string& name,
                                       const std::string& topic = "",
                                       bool isDirect = false,
                                       const std::vector<std::string>& inviteUserIds = {});

    // ---- Start DM (shortcut: createRoom + invite) ----

    // Creates a direct chat with a single user. If a DM already exists
    // with this user, we could return it (TODO: search m.direct). For now,
    // always creates a new room.
    ApiResult<std::string> startDirectMessage(const std::string& userId);

    // ---- Search users ----

    // GET /_matrix/client/v3/user_directory/search
    // Body: {"search_term": "query", "limit": 10}
    // Returns raw JSON: {"results": [{"user_id": "@user:server", "displayname": "Name"}]}
    ApiResult<std::string> searchUsers(const std::string& query, int limit = 10);

    // ---- Get user profile ----

    // GET /_matrix/client/v3/profile/{userId}
    // Returns raw JSON: {"displayname": "...", "avatar_url": "..."}
    ApiResult<std::string> getUserProfile(const std::string& userId);

    // ---- Join / Leave room ----

    // POST /_matrix/client/v3/join/{roomIdOrAlias}
    // Body: {"server_name": ["server1", ...]} optional via hints
    // Returns room_id from response.
    ApiResult<std::string> joinRoom(const std::string& roomIdOrAlias,
                                     const std::vector<std::string>& viaServers = {});

    // POST /_matrix/client/v3/rooms/{roomId}/leave
    ApiResult<bool> leaveRoom(const std::string& roomId);

    // ---- Reactions ----

    // POST /_matrix/client/v3/rooms/{roomId}/send/m.reaction/{txnId}
    // Body: {"m.relates_to":{"rel_type":"m.annotation","event_id":"...","key":"emoji"}}
    ApiResult<std::string> sendReaction(const std::string& roomId,
                                          const std::string& eventId,
                                          const std::string& emoji);

    // ---- Redact (delete) message ----

    // PUT /_matrix/client/v3/rooms/{roomId}/redact/{eventId}/{txnId}
    ApiResult<bool> redactEvent(const std::string& roomId,
                                 const std::string& eventId,
                                 const std::string& reason = "");

    // ---- Pin / Unpin message ----

    // PUT m.room.pinned_events state — appends/removes eventId from pinned list.
    // Uses getRoomState to fetch current pinned list, then PUTs the new one.
    ApiResult<bool> pinMessage(const std::string& roomId, const std::string& eventId);
    ApiResult<bool> unpinMessage(const std::string& roomId, const std::string& eventId);

    // ---- Room state (topic, name, etc.) ----

    // GET /_matrix/client/v3/rooms/{roomId}/state — all state events as raw JSON array.
    ApiResult<std::string> getRoomState(const std::string& roomId);

    // PUT /_matrix/client/v3/rooms/{roomId}/state/{eventType}/{stateKey}
    ApiResult<bool> sendStateEvent(const std::string& roomId,
                                    const std::string& eventType,
                                    const std::string& stateKey,
                                    const std::string& bodyJson);

    // PUT m.room.topic state
    ApiResult<bool> setRoomTopic(const std::string& roomId, const std::string& topic);

    // PUT m.room.name state
    ApiResult<bool> setRoomName(const std::string& roomId, const std::string& name);

    // ---- Room members ----

    // GET /_matrix/client/v3/rooms/{roomId}/members
    ApiResult<std::string> getRoomMembers(const std::string& roomId);

    // ---- Kick / Ban / Power levels ----

    // POST /_matrix/client/v3/rooms/{roomId}/kick  body: {"user_id":"...","reason":"..."}
    ApiResult<bool> kickUser(const std::string& roomId, const std::string& userId, const std::string& reason = "");

    // POST /_matrix/client/v3/rooms/{roomId}/ban  body: {"user_id":"...","reason":"..."}
    ApiResult<bool> banUser(const std::string& roomId, const std::string& userId, const std::string& reason = "");

    // POST /_matrix/client/v3/rooms/{roomId}/unban
    ApiResult<bool> unbanUser(const std::string& roomId, const std::string& userId);

    // PUT m.room.power_levels state — set a single user's power level.
    // Fetches current power_levels, modifies the user's level, PUTs back.
    ApiResult<bool> setUserPowerLevel(const std::string& roomId, const std::string& userId, int level);

    // ---- Threads ----

    // GET /_matrix/client/v1/rooms/{roomId}/threads?from=...&limit=...
    // Returns raw JSON: {"thread_root_ids":[...], "next_batch":"..."}
    ApiResult<std::string> getThreads(const std::string& roomId, const std::string& from = "", int limit = 20);

    // GET /_matrix/client/v3/rooms/{roomId}/relations/{eventId}/m.thread
    // Returns the thread's replies as a messages response (chunk + end).
    ApiResult<std::string> getThreadReplies(const std::string& roomId, const std::string& rootEventId);

    // ---- Public rooms directory ----

    // POST /_matrix/client/v3/publicRooms  body: {"server":"...","limit":20,"filter":{"generic_search_term":"..."}}
    // Returns raw JSON: {"chunk":[...], "next_batch":"..."}
    ApiResult<std::string> searchPublicRooms(const std::string& server, const std::string& query, int limit = 20, const std::string& from = "");

    // ---- Space hierarchy ----

    // GET /_matrix/client/v1/rooms/{spaceId}/hierarchy?max_depth=2
    // Returns raw JSON: {"rooms":[...], "next_batch":"..."}
    ApiResult<std::string> getSpaceHierarchy(const std::string& spaceId, int maxDepth = 2);

    // ---- Media download ----

    // Resolve mxc:// to HTTP thumbnail URL and download.
    // If width/height > 0, uses thumbnail endpoint; else full download.
    // Returns raw bytes of the image.
    ApiResult<std::vector<uint8_t>> downloadMedia(const std::string& mxcUrl,
                                                     int width = 0, int height = 0);

    // ---- Sync ----

    // GET /_matrix/client/v3/sync — long-poll.
    // Uses parseSyncResponse() from progressive_native/sync_handler.cpp
    // (which delegates to sync_models.cpp's parser).
    ApiResult<progressive::SyncResponse> sync(const std::string& since = "",
                                                int timeoutMs = 30000);

    // GET /_matrix/client/v3/sync — long-poll, fast simdjson-based parser.
    // 50-200x faster than sync() for large responses. Use this for the
    // background sync loop. Returns FastSyncResponse with string_views
    // into the parser's internal buffer (valid until the response is destroyed).
    struct FastSyncResult {
        bool ok = false;
        FastSyncResponse data{};
        progressive::MatrixError error;
        int httpStatus = 0;
    };
    FastSyncResult syncFast(const std::string& since = "", int timeoutMs = 30000);

    // ---- Account / session ----

    void setAccount(const AccountInfo& acct);
    const AccountInfo& account() const { return account_; }
    bool isLoggedIn() const { return !account_.accessToken.empty(); }

    void setSessionStore(SessionStore* store) { sessionStore_ = store; }

    // Persist current account to the session store (if set).
    bool persistSession();

    // Load saved account from session store. Returns true if restored.
    bool loadSavedSession();

    // ---- End-to-End Encryption (E2EE) ----

    // POST /_matrix/client/v3/keys/upload
    // Upload device keys + one-time keys. Body is JSON:
    //   {"device_keys":{...}, "one_time_keys":{...}}
    // Returns raw server response (with one_time_key_counts).
    ApiResult<std::string> uploadKeys(const std::string& body);

    // POST /_matrix/client/v3/keys/query
    // Query device keys for a list of users. Body:
    //   {"device_keys":{"@user:server":[]}}
    // Returns raw server response with device keys.
    ApiResult<std::string> queryKeys(const std::string& body);

    // POST /_matrix/client/v3/keys/claim
    // Claim one-time keys for a list of (user, device) pairs. Body:
    //   {"one_time_keys":{"@user:server":{"device_id":"signed_curve25519"}}}
    // Returns raw server response with claimed one-time keys.
    ApiResult<std::string> claimKeys(const std::string& body);

    // PUT /_matrix/client/v3/sendToDevice/{eventType}/{txnId}
    // Send a to-device event. Body:
    //   {"messages":{"@user:server":{"device_id":{"...content..."}}}}
    ApiResult<bool> sendToDevice(const std::string& eventType,
                                  const std::string& txnId,
                                  const std::string& body);

    // ---- Media upload ----

    // POST /_matrix/media/v3/upload?filename=...
    // Uploads a file's binary content. Returns the mxc:// URL on success.
    ApiResult<std::string> uploadMedia(const std::vector<uint8_t>& data,
                                         const std::string& filename,
                                         const std::string& contentType);

    // ---- Registration ----
    // POST /_matrix/client/v3/register?kind=user
    // Tries to register a new account. For servers without captcha (m.login.dummy),
    // this works in-app. For matrix.org (requires captcha), falls back to browser.
    // Returns true on success (account created + logged in).
    ApiResult<AccountInfo> registerAccount(const std::string& username,
                                              const std::string& password,
                                              const std::string& homeserverUrl);

    // ---- Profile ----

    // PUT /_matrix/client/v3/profile/{userId}/displayname
    ApiResult<bool> setDisplayName(const std::string& displayName);

    // PUT /_matrix/client/v3/profile/{userId}/avatar_url
    ApiResult<bool> setAvatarUrl(const std::string& mxcUrl);

    // GET /_matrix/client/v3/profile/{userId}
    // Returns JSON with displayname and avatar_url.
    ApiResult<std::string> getProfile(const std::string& userId);

    // ---- Send message with thread relation ----

    // PUT /_matrix/client/v3/rooms/{roomId}/send/m.room.encrypted/{txnId}
    // Sends a pre-encrypted m.room.encrypted event.
    // Same as sendMessage, but adds m.relates_to: {rel_type: "m.thread", event_id}
    ApiResult<std::string> sendThreadReply(const std::string& roomId,
                                              const std::string& body,
                                              const std::string& rootEventId,
                                              const std::string& msgtype = "m.text");

    // PUT /_matrix/client/v3/rooms/{roomId}/send/m.room.encrypted/{txnId}
    // Sends a pre-encrypted m.room.encrypted event.
    ApiResult<std::string> sendEncryptedEvent(const std::string& roomId,
                                                 const std::string& contentJson,
                                                 const std::string& txnId);

    // PUT /_matrix/client/v3/rooms/{roomId}/send/{eventType}/{txnId}
    // Sends an event with a pre-built content JSON. Used for m.image, m.file,
    // m.video, m.audio messages where the content includes mxc URL etc.
    ApiResult<std::string> sendMessageEvent(const std::string& roomId,
                                               const std::string& eventType,
                                               const std::string& contentJson);

private:
    AccountInfo account_;
    SessionStore* sessionStore_ = nullptr;

    // Build the standard auth header if logged in.
    std::unordered_map<std::string, std::string> authHeaders() const;
};

} // namespace progressive::desktop
