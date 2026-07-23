// src/ui/room/room_store.cpp — room operations extracted from MainWindow.
#include "room_store.hpp"
#include "room_data_loader.hpp"
#include "../../core/session_store.hpp"
#include "../../core/fast_sync.hpp"
#include "../../core/crypto/decryptor.hpp"
#include "../timeline/timeline_model.hpp"
#include "../room_list_model.hpp"
#include "../profile/room_members_dialog.hpp"
#include "../../core/fast_sync.hpp"
#include "../../core/crypto/decryptor.hpp"

#include <QMetaObject>
#include <QWidget>
#include "core/debug_log.hpp"
#include "core/thread_pool.hpp"
#include <sstream>
#include <cstdio>
#include <simdjson.h>

namespace progressive::desktop {

// ---- Local helpers (copied from main_window.cpp) ----

static std::string jsonUnescape(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (++i >= s.size()) break;
        char c = s[i];
        switch (c) {
            case 'n': out += '\n'; break; case 't': out += '\t'; break;
            case 'r': out += '\r'; break; case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case 'u': {
                if (i + 4 >= s.size()) { out += 'u'; break; }
                unsigned cp = 0;
                for (int j = 1; j <= 4; ++j) {
                    char hex = s[i+j]; cp <<= 4;
                    if (hex>='0'&&hex<='9')cp|=(hex-'0');
                    else if(hex>='a'&&hex<='f')cp|=(hex-'a'+10);
                    else if(hex>='A'&&hex<='F')cp|=(hex-'A'+10);
                    else{cp=0;break;}
                }
                i+=4;
                if(cp<0x80)out+=static_cast<char>(cp);
                else if(cp<0x800){out+=static_cast<char>(0xC0|(cp>>6));out+=static_cast<char>(0x80|(cp&0x3F));}
                else{out+=static_cast<char>(0xE0|(cp>>12));out+=static_cast<char>(0x80|((cp>>6)&0x3F));out+=static_cast<char>(0x80|(cp&0x3F));}
                break;
            }
            default: out += '\\'; out += c; break;
        }
    }
    return out;
}

std::string extractStringDec(std::string_view json, const std::string& key) {
    std::string p = "\"" + key + "\":\"";
    auto pos = json.find(p);
    if (pos == std::string_view::npos) { p = "\"" + key + "\": \""; pos = json.find(p); }
    if (pos != std::string_view::npos) { pos += p.size(); size_t end = pos;
        while (end < json.size()) { if (json[end]=='\\'&&end+1<json.size()){end+=2;continue;} if (json[end]=='"')break; ++end; }
        if (end < json.size()) return jsonUnescape(std::string(json.substr(pos, end-pos))); }
    return {};
}

static std::string extractString(std::string_view json, const std::string& key) {
    return extractStringDec(json, key);
}

// Forward declarations
static void fastEventToDisplayed(const FastEvent& e, DisplayedEvent& de,
                                  const std::string& currentRoomId, Decryptor* decryptor);
static void appendTimelineForRoom(const std::string& roomId,
    const std::vector<FastEvent>& events, TimelineModel* model,
    const std::unordered_map<std::string,std::string>* memberAvatars,
    const std::string& myUserId);

std::string threadRootId(std::string_view json) {
    auto p = json.find("\"m.relates_to\"");
    if (p == std::string_view::npos) return {};
    auto s = json.find('{', p); if (s == std::string_view::npos) return {};
    int d = 0; size_t e = s;
    for (; e < json.size(); ++e) { if(json[e]=='{')d++; else if(json[e]=='}'){d--;if(d==0){e++;break;}} }
    auto obj = json.substr(s, e-s);
    auto rt = obj.find("\"rel_type\":\"m.thread\"");
    if (rt == std::string_view::npos) rt = obj.find("\"rel_type\": \"m.thread\"");
    if (rt == std::string_view::npos) return {};
    auto ei = obj.find("\"event_id\":\""); if (ei == std::string_view::npos) ei = obj.find("\"event_id\": \"");
    if (ei == std::string_view::npos) return {};
    ei += (obj[ei+11]=='"' ? 12 : 13);
    auto ee = obj.find('"', ei);
    if (ee == std::string_view::npos) return {};
    return std::string(obj.substr(ei, ee-ei));
}

std::string msgType(std::string_view json) { return extractStringDec(json, "msgtype"); }
std::string msgBody(std::string_view json) { return extractStringDec(json, "body"); }

// ---- RoomStore ----

RoomStore::RoomStore(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store)
    : client_(std::move(client)), store_(std::move(store)), dataLoader_(std::make_unique<RoomDataLoader>(client_, store_)) {}

void RoomStore::setClient(std::shared_ptr<MatrixClient> c) {
    client_ = std::move(c);
    if (dataLoader_) dataLoader_->setClient(client_);
}

void RoomStore::setSessionStore(std::shared_ptr<SessionStore> s) {
    store_ = std::move(s);
    if (dataLoader_) dataLoader_->setSessionStore(store_);
}

RoomMeta RoomStore::extractRoomMeta(const FastRoom& room, const std::string& myUserId) {
    RoomMeta m;
    for (const auto& e : room.stateEvents) {
        if (e.type == "m.room.name" && m.name.empty() && !e.contentJson.empty())
            m.name = extractStringDec(e.contentJson, "name");
        else if (e.type == "m.room.avatar" && m.avatarUrl.empty() && !e.contentJson.empty())
            m.avatarUrl = extractStringDec(e.contentJson, "url");
        else if (e.type == "m.room.canonical_alias" && m.canonicalAlias.empty() && !e.contentJson.empty())
            m.canonicalAlias = extractStringDec(e.contentJson, "alias");
        else if (e.type == "m.room.encryption") m.isEncrypted = true;
        else if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto mem = extractString(e.contentJson, "membership");
            if (mem == "join" && std::string(e.stateKey) != myUserId) {
                if (m.dmDisplayName.empty()) m.dmDisplayName = extractString(e.contentJson, "displayname");
                if (m.dmAvatarUrl.empty()) m.dmAvatarUrl = extractStringDec(e.contentJson, "avatar_url");
            }
        }
    }
    for (const auto& e : room.timeline.events) {
        if (m.name.empty() && e.type == "m.room.name" && !e.contentJson.empty())
            m.name = extractStringDec(e.contentJson, "name");
        if (m.avatarUrl.empty() && e.type == "m.room.avatar" && !e.contentJson.empty())
            m.avatarUrl = extractStringDec(e.contentJson, "url");
        if (m.canonicalAlias.empty() && e.type == "m.room.canonical_alias" && !e.contentJson.empty())
            m.canonicalAlias = extractStringDec(e.contentJson, "alias");
    }
    return m;
}

std::string RoomStore::extractLastMessageBody(const std::vector<FastEvent>& events) {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (it->type == "m.room.message" && !it->contentJson.empty()) {
            auto pos = it->contentJson.find("\"body\":\"");
            if (pos != std::string_view::npos) { pos += 8; size_t end = pos;
                while (end < it->contentJson.size()) { if (it->contentJson[end]=='\\'&&end+1<it->contentJson.size()){end+=2;continue;} if (it->contentJson[end]=='"')break; ++end; }
                if (end < it->contentJson.size()) return std::string(it->contentJson.substr(pos, end-pos)); }
        }
    }
    return {};
}

// ---- Sync update (worker thread part: no model access) ----

RoomSyncUpdate RoomStore::prepareRoomSyncUpdate(const FastSyncResponse& resp,
                                                  const std::string& currentRoomId,
                                                  const std::string& myUserId) {
    RoomSyncUpdate u;

    // Left rooms
    for (const auto& leftId : resp.leftRoomIds)
        u.roomsToRemove.push_back(std::string(leftId));

    // Joined rooms
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomMeta meta = extractRoomMeta(room, myUserId);

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
        rd.lastMessage = jsonUnescape(extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 : room.timeline.events.back().originServerTs;
        rd.unreadCount = room.notificationCount;
        rd.highlightCount = room.highlightCount;
        for (auto& tu : room.typingUsers) rd.typingUsers.push_back(std::string(tu));

        // Store last notification body for highlights
        if (room.highlightCount > 0 && !room.timeline.events.empty()) {
            u.lastNotificationBody = extractLastMessageBody(room.timeline.events);
        }

        u.roomsToUpsert.push_back(std::move(rd));

        // Store timeline events for current room
        if (roomId == currentRoomId && !room.timeline.events.empty()) {
            u.currentRoomUpdated = true;
            u.currentRoomId = roomId;
            u.currentRoomEvents = room.timeline.events;
            for (const auto& e : room.stateEvents) {
                if (e.type == "m.room.member" && !e.contentJson.empty()) {
                    auto av = extractStringDec(e.contentJson, "avatar_url");
                    if (!av.empty()) u.currentRoomAvatars[std::string(e.stateKey)] = av;
                }
            }
            for (const auto& e : room.timeline.events) {
                if (e.type == "m.room.member" && !e.contentJson.empty()) {
                    auto av = extractStringDec(e.contentJson, "avatar_url");
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
        u.invitedRooms.push_back(std::move(rd));
    }

    u.inviteCount = static_cast<int>(u.invitedRooms.size());
    if (u.inviteCount > 0)
        u.inviteText = QString(" ⬇ Invitations (%1) ").arg(u.inviteCount);

    return u;
}

// ---- Sync update (UI thread part: model operations only) ----

void RoomStore::applyRoomSyncUpdate(RoomSyncUpdate& syncUpdate,
                                     RoomListModel* roomList,
                                     TimelineModel* currentTimeline) {
    for (const auto& rid : syncUpdate.roomsToRemove) {
        roomList->removeRoom(rid);
    }

    for (auto& rd : syncUpdate.roomsToUpsert) {
        int existingRow = roomList->findRowByRoomId(rd.roomId);
        const RoomData* existing = existingRow >= 0 ? roomList->at(existingRow) : nullptr;

        // Preserve old name/avatar if sync didn't provide new ones
        if (rd.name == rd.roomId && existing && !existing->name.empty() && existing->name != rd.roomId)
            rd.name = existing->name;
        if (rd.avatarUrl.empty() && existing && !existing->avatarUrl.empty())
            rd.avatarUrl = existing->avatarUrl;
        if (!rd.isEncrypted && existing && existing->isEncrypted)
            rd.isEncrypted = true;

        roomList->upsertRoom(rd);
    }

    for (auto& rd : syncUpdate.invitedRooms) {
        roomList->upsertRoom(rd);
    }

    // Timeline for current room
    if (syncUpdate.currentRoomUpdated && currentTimeline) {
        appendTimelineForRoom(syncUpdate.currentRoomId, syncUpdate.currentRoomEvents,
                              currentTimeline, &syncUpdate.currentRoomAvatars,
                              "" /* myUserId passed earlier */);
    }
}

void RoomStore::loadHistory(const std::string& roomId, TimelineModel* model,
                              LifeToken token,
                              std::function<void(int, const std::string&)> callback) {
    dataLoader_->loadHistory(roomId, model, token, callback);
}

void RoomStore::loadMembers(const std::string& roomId, LifeToken token,
                              const std::vector<std::string>& relevantIds,
                              std::function<void(std::vector<MemberInfo>)> callback) {
    dataLoader_->loadMembers(roomId, token, relevantIds, callback);
}

void RoomStore::batchLoadRoomStates(RoomListModel* model, LifeToken token) {
    dataLoader_->batchLoadRoomStates(model, token);
}

static void fastEventToDisplayed(const FastEvent& e, DisplayedEvent& de,
                                       const std::string& currentRoomId,
                                       Decryptor* decryptor) {
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
        auto result = decryptor->decryptMegolmEvent(currentRoomId, de.senderId, de.contentJson);
        if (result.ok) {
            simdjson::dom::parser parser;
            auto root = parser.parse(result.plaintext);
            if (root.error() == simdjson::SUCCESS) {
                auto t = root.value()["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());
                auto cr = root.value()["content"];
                if (cr.error() == simdjson::SUCCESS) de.contentJson = simdjson::to_string(cr.value());
            }
        } else { de.body = "[encrypted]"; de.msgtype = "m.notice"; }
    }
    if (de.type == "m.room.message") {
        // Parse content with simdjson for correct extraction (works for ALL clients)
        simdjson::dom::parser p;
        auto doc = p.parse(de.contentJson);
        if (doc.error() == simdjson::SUCCESS) {
            auto val = doc.value();
            auto bodyStr = val["body"].get_string();
            if (bodyStr.error() == simdjson::SUCCESS)
                de.body = jsonUnescape(std::string(bodyStr.value()));
            auto msgStr = val["msgtype"].get_string();
            if (msgStr.error() == simdjson::SUCCESS)
                de.msgtype = std::string(msgStr.value());
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
        auto thRoot = threadRootId(de.contentJson);
        if (!thRoot.empty()) { de.isThreadReply = true; de.threadRootId = thRoot; }
    }
    if (de.type == "m.room.encrypted") {
        LOG(LogChannel::DBG, "sync-encrypted: sender=%s content=[%.300s]",
            de.senderId.c_str(), de.contentJson.c_str());
    }
    // Catch-all: log every event that passes through sync path
    LOG(LogChannel::DBG, "sync-event: type=%s bodyEmpty=%d contentEmpty=%d sender=%.30s body=[%.100s]",
        de.type.c_str(), (int)de.body.empty(), (int)de.contentJson.empty(),
        de.senderId.c_str(), de.body.c_str());
}

std::string makeSystemBody(const std::string& type, const std::string& contentJson,
                                    const std::string& stateKey) {
    if (type == "m.room.member") {
        std::string displayName = stateKey;
        auto colon = displayName.find(':');
        if (colon != std::string::npos && colon > 0 && displayName[0] == '@')
            displayName = displayName.substr(1, colon - 1);
        auto ms = extractString(contentJson, "membership");
        if (ms == "join")      return displayName + " joined";
        else if (ms == "leave") return displayName + " left";
        else if (ms == "invite") return displayName + " was invited";
        else if (ms == "ban")   return displayName + " was banned";
        else return "";
    }
    else if (type == "m.room.topic") {
        auto topic = extractString(contentJson, "topic");
        return "Topic changed" + (topic.empty() ? "" : ": " + topic);
    }
    else if (type == "m.room.name") {
        auto name = extractString(contentJson, "name");
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

static DisplayedEvent makeSystemEvent(const FastEvent& e) {
    DisplayedEvent sys;
    sys.type = "progressive.system";
    sys.eventId = std::string(e.eventId);
    sys.originServerTs = e.originServerTs;
    sys.senderName = "system";
    std::string stateKey(e.stateKey.data(), e.stateKey.size());
    sys.body = makeSystemBody(std::string(e.type), std::string(e.contentJson), stateKey);
    return sys;
}

static void appendTimelineForRoom(const std::string& roomId,
    const std::vector<FastEvent>& events, TimelineModel* model,
    const std::unordered_map<std::string,std::string>* memberAvatars,
    const std::string& myUserId) {
    std::vector<DisplayedEvent> batch;
    for (const auto& e : events) {
        if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto ms = extractString(e.contentJson, "membership");
            if (ms == "join" && std::string(e.stateKey) == myUserId) continue;
        }
        if (e.type == "m.room.redaction" && !e.contentJson.empty()) {
            auto rid = extractStringDec(e.contentJson, "redacts");
            if (!rid.empty()) model->markDeleted(rid);
            continue;
        }
        if (e.type == "m.reaction" && !e.contentJson.empty()) {
            simdjson::dom::parser rp;
            auto doc = rp.parse(std::string(e.contentJson));
            if (doc.error() == simdjson::SUCCESS) {
                auto rel = doc.value()["m.relates_to"];
                auto te = rel["event_id"].get_string();
                auto key = rel["key"].get_string();
                if (te.error() == simdjson::SUCCESS && key.error() == simdjson::SUCCESS)
                    model->addReaction(std::string(te.value()), std::string(key.value()), std::string(e.senderId));
            }
            continue;
        }
        if (e.type == "m.room.member" || e.type == "m.room.topic" ||
            e.type == "m.room.name" || e.type == "m.room.encryption" ||
            e.type == "m.room.create" || e.type == "m.room.avatar") {
            DisplayedEvent sys = makeSystemEvent(e);
            if (!sys.body.empty()) batch.push_back(sys);
            continue;
        }
        if (e.type == "m.typing" || e.type == "m.receipt" || e.type == "m.fully_read") {
            continue;
        }
        if (e.type != "m.room.message" && e.type != "m.room.encrypted") continue;
        DisplayedEvent de;
        fastEventToDisplayed(e, de, roomId, nullptr);
        if (memberAvatars && !de.senderId.empty()) {
            auto it = memberAvatars->find(de.senderId);
            if (it != memberAvatars->end()) de.avatarUrl = it->second;
        }
        batch.push_back(std::move(de));
        if (de.isThreadReply && !de.threadRootId.empty()) {
            int rootRow = model->findRow(de.threadRootId);
            if (rootRow >= 0) {
                auto* revt = model->at(rootRow);
                if (revt) { revt->threadReplyCount++; emit model->dataChanged(model->index(rootRow), model->index(rootRow)); }
            }
        }
    }
    model->appendBackBatch(batch);
}

} // namespace progressive::desktop
