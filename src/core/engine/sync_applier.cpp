// src/core/engine/sync_applier.cpp — moved from room_store (X1 phase 3).
// Worker-thread pure logic: sync response -> RoomSyncUpdate delta.
#include "sync_applier.hpp"

#include "../crypto/decryptor.hpp"
#include "core/debug_log.hpp"
#include "core/json_utils.hpp"

#include <regex>
#include <simdjson.h>

namespace progressive::desktop {


// Parse a decrypted Megolm plaintext JSON into type + content JSON.
static bool parsePlaintextBody(const std::string& plaintextJson,
                               std::string& outType,
                               std::string& outContentJson) {
    simdjson::dom::parser parser;
    auto root = parser.parse(plaintextJson);
    if (root.error() != simdjson::SUCCESS) return false;
    auto t = root.value()["type"].get_string();
    if (t.error() != simdjson::SUCCESS) return false;
    outType = std::string(t.value());
    auto cr = root.value()["content"];
    if (cr.error() == simdjson::SUCCESS) {
        outContentJson = simdjson::to_string(cr.value());
    }
    return true;
}

std::string SyncApplier::extractStringDec(std::string_view json, const std::string& key) {
    std::string p = "\"" + key + "\":\"";
    auto pos = json.find(p);
    if (pos == std::string_view::npos) { p = "\"" + key + "\": \""; pos = json.find(p); }
    if (pos != std::string_view::npos) { pos += p.size(); size_t end = pos;
        while (end < json.size()) { if (json[end]=='\\'&&end+1<json.size()){end+=2;continue;} if (json[end]=='"')break; ++end; }
        if (end < json.size()) return jsonUnescape(std::string(json.substr(pos, end-pos))); }
    return {};
}

std::string SyncApplier::extractString(std::string_view json, const std::string& key) {
    return SyncApplier::extractStringDec(json, key);
}

std::string SyncApplier::extractThreadRootId(std::string_view json) {
    simdjson::dom::parser p;
    auto doc = p.parse(json);
    if (doc.error() != simdjson::SUCCESS) return {};
    auto rel = doc.value()["m.relates_to"];
    if (rel.error() != simdjson::SUCCESS) return {};
    auto rt = rel["rel_type"].get_string();
    if (rt.error() != simdjson::SUCCESS || std::string_view(rt.value()) != "m.thread") return {};
    auto eid = rel["event_id"].get_string();
    if (eid.error() != simdjson::SUCCESS) return {};
    return std::string(eid.value());
}

std::string SyncApplier::extractReplyToId(std::string_view contentJson) {
    simdjson::dom::parser p;
    auto doc = p.parse(contentJson);
    if (doc.error() != simdjson::SUCCESS) return {};
    auto rel = doc.value()["m.relates_to"];
    if (rel.error() != simdjson::SUCCESS) return {};
    auto rt = rel["rel_type"].get_string();
    if (rt.error() == simdjson::SUCCESS && std::string_view(rt.value()) == "m.in_reply_to") {
        auto eid = rel["event_id"].get_string();
        if (eid.error() != simdjson::SUCCESS) return {};
        return std::string(eid.value());
    }
    return {};
}

std::string SyncApplier::msgType(std::string_view json) { return SyncApplier::extractStringDec(json, "msgtype"); }
std::string SyncApplier::msgBody(std::string_view json) { return SyncApplier::extractStringDec(json, "body"); }

// ---- RoomStore ----

RoomMeta SyncApplier::extractRoomMeta(const FastRoom& room, const std::string& myUserId) {
    RoomMeta m;
    for (const auto& e : room.stateEvents) {
        if (e.type == "m.room.name" && m.name.empty() && !e.contentJson.empty())
            m.name = SyncApplier::extractStringDec(e.contentJson, "name");
        else if (e.type == "m.room.avatar" && m.avatarUrl.empty() && !e.contentJson.empty())
            m.avatarUrl = SyncApplier::extractStringDec(e.contentJson, "url");
        else if (e.type == "m.room.canonical_alias" && m.canonicalAlias.empty() && !e.contentJson.empty())
            m.canonicalAlias = SyncApplier::extractStringDec(e.contentJson, "alias");
        else if (e.type == "m.room.encryption") m.isEncrypted = true;
        else if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto mem = SyncApplier::extractString(e.contentJson, "membership");
            if (mem == "join" && std::string(e.stateKey) != myUserId) {
                if (m.dmDisplayName.empty()) m.dmDisplayName = SyncApplier::extractString(e.contentJson, "displayname");
                if (m.dmAvatarUrl.empty()) m.dmAvatarUrl = SyncApplier::extractStringDec(e.contentJson, "avatar_url");
            }
        }
    }
    for (const auto& e : room.timeline.events) {
        if (m.name.empty() && e.type == "m.room.name" && !e.contentJson.empty())
            m.name = SyncApplier::extractStringDec(e.contentJson, "name");
        if (m.avatarUrl.empty() && e.type == "m.room.avatar" && !e.contentJson.empty())
            m.avatarUrl = SyncApplier::extractStringDec(e.contentJson, "url");
        if (m.canonicalAlias.empty() && e.type == "m.room.canonical_alias" && !e.contentJson.empty())
            m.canonicalAlias = SyncApplier::extractStringDec(e.contentJson, "alias");
    }
    return m;
}

std::string SyncApplier::extractLastMessageBody(const std::vector<FastEvent>& events) {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (it->type == "m.room.message" && !it->contentJson.empty()) {
            simdjson::dom::parser p;
            auto doc = p.parse(it->contentJson);
            if (doc.error() != simdjson::SUCCESS) continue;
            auto b = doc.value()["body"].get_string();
            if (b.error() == simdjson::SUCCESS) return std::string(b.value());
        }
    }
    return {};
}

RoomSyncUpdate SyncApplier::prepareRoomSyncUpdate(const FastSyncResponse& resp,
                                                  const std::string& currentRoomId,
                                                  const std::string& myUserId) {
    RoomSyncUpdate u;

    // Left rooms
    for (const auto& leftId : resp.leftRoomIds)
        u.roomsToRemove.push_back(std::string(leftId));

    // Joined rooms
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomMeta meta = SyncApplier::extractRoomMeta(room, myUserId);

        RoomData rd;
        rd.roomId = roomId;
        // Name: meta.name → canonicalAlias → dmDisplayName → roomId
        rd.name = meta.name.empty()
            ? (meta.canonicalAlias.empty()
                ? (meta.dmDisplayName.empty() ? roomId : meta.dmDisplayName)
                : meta.canonicalAlias)
            : meta.name;
        rd.avatarUrl = meta.avatarUrl.empty() ? meta.dmAvatarUrl : meta.avatarUrl;
        rd.isEncrypted = meta.isEncrypted || room.isEncrypted;
        rd.lastMessage = jsonUnescape(SyncApplier::extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 : room.timeline.events.back().originServerTs;
        rd.unreadCount = room.notificationCount;
        rd.highlightCount = room.highlightCount;
        if (roomId == currentRoomId) {
            rd.unreadCount = 0;
            rd.highlightCount = 0;
        }
        for (auto& tu : room.typingUsers) rd.typingUsers.push_back(std::string(tu));

        // Store last notification body for highlights
        if (room.highlightCount > 0 && !room.timeline.events.empty()) {
            u.lastNotificationBody = SyncApplier::extractLastMessageBody(room.timeline.events);
        }

        u.roomsToUpsert.push_back(std::move(rd));

        // Store timeline events for current room
        if (roomId == currentRoomId && !room.timeline.events.empty()) {
            u.currentRoomUpdated = true;
            u.currentRoomId = roomId;
            u.currentRoomEvents = room.timeline.events;
            for (const auto& e : room.stateEvents) {
                if (e.type == "m.room.member" && !e.contentJson.empty()) {
                    auto av = SyncApplier::extractStringDec(e.contentJson, "avatar_url");
                    if (!av.empty()) u.currentRoomAvatars[std::string(e.stateKey)] = av;
                }
            }
            for (const auto& e : room.timeline.events) {
                if (e.type == "m.room.member" && !e.contentJson.empty()) {
                    auto av = SyncApplier::extractStringDec(e.contentJson, "avatar_url");
                    if (!av.empty()) u.currentRoomAvatars[std::string(e.stateKey)] = av;
                }
            }
        }
    }

    // Invites
    for (const auto& inv : resp.invitedRooms) {
        std::string roomId(inv.roomId);
        RoomData rd;
        rd.roomId = roomId;
        rd.isInvite = true;
        rd.name = inv.roomName.empty() ? roomId : std::string(inv.roomName);
        rd.inviterId = std::string(inv.inviterId);
        if (!inv.inviterId.empty()) {
            std::string n = rd.inviterId;
            if (n[0] == '@') { auto c = n.find(':'); if (c != std::string::npos) n = n.substr(1, c-1); else n = n.substr(1); }
            rd.lastMessage = n + " invited you";
        }
        if (!inv.roomAvatar.empty()) rd.avatarUrl = std::string(inv.roomAvatar);
        rd.isEncrypted = inv.isEncrypted;
        rd.memberCount = inv.memberCount;
        if (rd.memberCount > 0) {
            rd.lastMessage += " · " + std::to_string(rd.memberCount) + " member" + (rd.memberCount > 1 ? "s" : "");
        }
        u.invitedRooms.push_back(std::move(rd));
    }

    u.inviteCount = static_cast<int>(u.invitedRooms.size());

    return u;
}

void SyncApplier::fastEventToDisplayed(const FastEvent& e, DisplayedEvent& de,
                                       const std::string& currentRoomId,
                                       Decryptor* decryptor) {
    std::fprintf(stderr, "[fast-cp] enter type=%.20s\n", std::string(e.type).c_str());
    de.eventId = std::string(e.eventId);
    de.senderId = std::string(e.senderId);
    de.type = std::string(e.type);
    de.contentJson = std::string(e.contentJson);
    de.originServerTs = e.originServerTs;
    if (!de.senderId.empty() && de.senderId[0] == '@') {
        auto colon = de.senderId.find(':');
        de.senderName = (colon != std::string::npos) ? de.senderId.substr(1, colon-1) : de.senderId.substr(1);
    }
    if (de.type == "m.room.encrypted" && decryptor && decryptor->isInitialized()) {
        LOG(LogChannel::E2EE, "fastEventToDisplayed: decrypting eid=%s", de.eventId.c_str());
        auto result = decryptor->decryptMegolmEvent(currentRoomId, de.senderId, de.contentJson, de.eventId, de.originServerTs);
        if (result.ok) {
            LOG(LogChannel::E2EE, "fastEventToDisplayed: DECRYPTED eid=%s", de.eventId.c_str());
            parsePlaintextBody(result.plaintext, de.type, de.contentJson);
        } else {
            LOG(LogChannel::E2EE, "fastEventToDisplayed: FAILED eid=%s err=%s",
                de.eventId.c_str(), result.error.c_str());
            de.body = "[encrypted]"; de.msgtype = "m.notice";
        }
    } else if (de.type == "m.room.encrypted") {
        LOG(LogChannel::E2EE, "fastEventToDisplayed: SKIP decryptor=%p init=%d",
            (void*)decryptor, decryptor ? decryptor->isInitialized() : 0);
    }
    std::fprintf(stderr, "[fast-cp] before message branch\n");
    if (de.type == "m.room.message") {
        // Parse content with simdjson for correct extraction (works for ALL clients)
        std::fprintf(stderr, "[fast-cp] parsing content\n");
        simdjson::dom::parser p;
        auto doc = p.parse(de.contentJson);
        std::fprintf(stderr, "[fast-cp] parsed err=%d\n", (int)doc.error());
        if (doc.error() == simdjson::SUCCESS) {
            auto val = doc.value();
            std::string keys;
            for (auto [k, v] : val.get_object().value()) {
                keys += std::string(k) + ",";
            }
            LOG(LogChannel::DBG, "sync-fluffy: content keys=[%s] content=[%.200s]",
                keys.c_str(), de.contentJson.c_str());
            auto bodyStr = val["body"].get_string();
            if (bodyStr.error() == simdjson::SUCCESS) {
                de.body = jsonUnescape(std::string(bodyStr.value()));
            } else {
                auto fb = val["formatted_body"].get_string();
                if (fb.error() == simdjson::SUCCESS) {
                    std::string fbBody = jsonUnescape(std::string(fb.value()));
                    de.body = std::regex_replace(fbBody, std::regex("<[^>]*>"), "");
                } else {
                    auto fbBody = val["formatted_body"]["body"].get_string();
                    if (fbBody.error() == simdjson::SUCCESS) {
                        std::string fbBodyStr = jsonUnescape(std::string(fbBody.value()));
                        de.body = std::regex_replace(fbBodyStr, std::regex("<[^>]*>"), "");
                    }
                }
            }
            auto msgStr = val["msgtype"].get_string();
            std::fprintf(stderr, "[fast-cp] msgtype err=%d\n", (int)msgStr.error());
            if (msgStr.error() == simdjson::SUCCESS)
                de.msgtype = std::string(msgStr.value());
            std::fprintf(stderr, "[fast-cp] msgtype assigned=%s\n", de.msgtype.c_str());
            if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                auto url = val["url"].get_string();
                if (url.error() == simdjson::SUCCESS)
                    de.mxcUrl = jsonUnescape(std::string(url.value()));
                auto mime = val["mimetype"].get_string();
                if (mime.error() == simdjson::SUCCESS)
                    de.mimetype = std::string(mime.value());
            }
        }
        if (de.body.empty() && !de.contentJson.empty()) {
            LOG(LogChannel::DBG, "sync-empty-body: m.room.message sender=%s content=[%.300s]",
                de.senderId.c_str(), de.contentJson.c_str());
        }
        auto thRoot = SyncApplier::extractThreadRootId(de.contentJson);
        if (!thRoot.empty()) { de.isThreadReply = true; de.threadRootId = thRoot; }
        std::fprintf(stderr, "[fast-cp] message branch done\n");
    }
    if (de.type == "m.room.encrypted") {
        LOG(LogChannel::DBG, "sync-encrypted: sender=%s content=[%.300s]",
            de.senderId.c_str(), de.contentJson.c_str());
    }
    std::fprintf(stderr, "[fast-cp] end\n");
    // Catch-all: log every event that passes through sync path
    LOG(LogChannel::DBG, "sync-event: type=%s bodyEmpty=%d contentEmpty=%d sender=%.30s body=[%.100s]",
        de.type.c_str(), (int)de.body.empty(), (int)de.contentJson.empty(),
        de.senderId.c_str(), de.body.c_str());
}

std::string SyncApplier::makeSystemBody(const std::string& type, const std::string& contentJson,
                                    const std::string& stateKey) {
    if (type == "m.room.member") {
        std::string displayName = stateKey;
        auto colon = displayName.find(':');
        if (colon != std::string::npos && colon > 0 && displayName[0] == '@')
            displayName = displayName.substr(1, colon - 1);
        auto ms = SyncApplier::extractString(contentJson, "membership");
        if (ms == "join")      return displayName + " joined";
        else if (ms == "leave") return displayName + " left";
        else if (ms == "invite") return displayName + " was invited";
        else if (ms == "ban")   return displayName + " was banned";
        else return "";
    }
    else if (type == "m.room.topic") {
        auto topic = SyncApplier::extractString(contentJson, "topic");
        return "Topic changed" + (topic.empty() ? "" : ": " + topic);
    }
    else if (type == "m.room.name") {
        auto name = SyncApplier::extractString(contentJson, "name");
        return "Room renamed to " + (name.empty() ? "(removed)" : name);
    }
    else if (type == "m.room.encryption") {
        return "Encryption enabled";
    }
    else if (type == "m.room.create") {
        return "Room created";
    }
    else if (type == "m.room.avatar") {
        return "Avatar changed";
    }
    return "";
}

bool SyncApplier::decryptEventToDisplayed(const FastEvent& fe, DisplayedEvent& de,
                                             const std::string& currentRoomId,
                                             Decryptor* decryptor) {
    fastEventToDisplayed(fe, de, currentRoomId, decryptor);
    return true;
}

} // namespace progressive::desktop
