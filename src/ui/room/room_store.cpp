// src/ui/room/room_store.cpp — room operations extracted from MainWindow.
#include "room_store.hpp"
#include "../../core/session_store.hpp"
#include "../../core/fast_sync.hpp"
#include "../../core/crypto/decryptor.hpp"
#include "../timeline_model.hpp"
#include "../room_list_model.hpp"
#include "../room_members_dialog.hpp"
#include "../../core/fast_sync.hpp"
#include "../../core/crypto/decryptor.hpp"

#include <QMetaObject>
#include <QWidget>
#include <thread>
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

RoomStore::RoomStore(MatrixClient* client, SessionStore* store)
    : client_(client), store_(store) {}

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

void RoomStore::rebuildFromSync(const FastSyncResponse& resp,
                                  RoomListModel* roomList, TimelineModel* currentTimeline,
                                  const std::string& currentRoomId, const std::string& myUserId,
                                  QString& inviteText_out, bool& hasInvites_out) {
    // Remove left rooms
    for (const auto& leftId : resp.leftRoomIds) {
        roomList->removeRoom(std::string(leftId));
        if (currentRoomId == std::string(leftId))
            currentTimeline->clear();
    }

    // Joined rooms
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomMeta meta = extractRoomMeta(room, myUserId);
        RoomData rd;
        rd.roomId = roomId;
        rd.name = meta.name.empty() ? (meta.canonicalAlias.empty() ? (meta.dmDisplayName.empty() ? roomId : meta.dmDisplayName) : meta.canonicalAlias) : meta.name;
        rd.avatarUrl = meta.avatarUrl.empty() ? meta.dmAvatarUrl : meta.avatarUrl;
        rd.isEncrypted = meta.isEncrypted || room.isEncrypted;
        rd.lastMessage = jsonUnescape(extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 : room.timeline.events.back().originServerTs;
        rd.unreadCount = room.notificationCount;
        rd.highlightCount = room.highlightCount;
        rd.typingUsers.clear();
        for (auto& tu : room.typingUsers) rd.typingUsers.push_back(std::string(tu));

        int existingRow = roomList->findRowByRoomId(roomId);
        if (existingRow >= 0) {
            RoomData* old = const_cast<RoomData*>(roomList->at(existingRow));
            if (old) {
                if (!rd.name.empty() && rd.name != roomId) old->name = rd.name;
                if (!rd.avatarUrl.empty()) old->avatarUrl = rd.avatarUrl;
                old->lastMessage = rd.lastMessage;
                old->lastActivityTs = rd.lastActivityTs;
                old->unreadCount = rd.unreadCount;
                old->highlightCount = rd.highlightCount;
                old->isEncrypted = rd.isEncrypted;
                old->typingUsers = rd.typingUsers;
                emit roomList->dataChanged(roomList->index(existingRow), roomList->index(existingRow));
            }
        } else {
            roomList->upsertRoom(rd);
        }

        // Timeline for current room
        if (currentRoomId == roomId && !room.timeline.events.empty()) {
            std::unordered_map<std::string, std::string> memberAvatars;
            for (const auto& e : room.stateEvents) {
                if (e.type == "m.room.member" && !e.contentJson.empty()) {
                    auto av = extractStringDec(e.contentJson, "avatar_url");
                    if (!av.empty()) memberAvatars[std::string(e.stateKey)] = av;
                }
            }
            appendTimelineForRoom(roomId, room.timeline.events, currentTimeline, &memberAvatars, myUserId);
        }
    }

    // Invites
    int inviteCount = 0;
    hasInvites_out = false;
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
        roomList->upsertRoom(rd);
        inviteCount++;
    }
    if (inviteCount > 0) {
        inviteText_out = QString(" ⬇ Invitations (%1) ").arg(inviteCount);
        hasInvites_out = true;
    }
}

void RoomStore::loadHistory(const std::string& roomId, TimelineModel* model,
                              QPointer<QWidget> guard, std::function<void(bool)> callback) {
    if (!client_ || !client_->isLoggedIn()) { if (callback) callback(false); return; }
    MatrixClient* c = client_;
    std::thread([guard, c, roomId, model, callback]() {
        auto result = c->getMessages(roomId, "", 30);
        QMetaObject::invokeMethod(guard, [guard, result, model, callback]() {
            if (guard.isNull()) return;
            if (!result.ok) { if (callback) callback(false); return; }
            simdjson::dom::parser parser;
            auto root = parser.parse(result.data);
            if (root.error() != simdjson::SUCCESS) { if (callback) callback(false); return; }
            auto chunk = root.value()["chunk"].get_array();
            if (chunk.error() != simdjson::SUCCESS) { if (callback) callback(false); return; }
            std::vector<DisplayedEvent> events;
            for (auto evt : chunk.value()) {
                DisplayedEvent de;
                auto t = evt["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());
                auto eid = evt["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) de.eventId = std::string(eid.value());
                auto sender = evt["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) de.senderId = std::string(sender.value());
                auto ts = evt["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) de.originServerTs = ts.value();
                auto cr = evt["content"];
                if (cr.error() == simdjson::SUCCESS) de.contentJson = simdjson::to_string(cr.value());
                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto c = de.senderId.find(':');
                    de.senderName = (c != std::string::npos) ? de.senderId.substr(1, c-1) : de.senderId.substr(1);
                }
                if (de.type == "m.room.message") {
                    de.msgtype = msgType(de.contentJson);
                    de.body = msgBody(de.contentJson);
                }
                events.push_back(std::move(de));
            }
            std::reverse(events.begin(), events.end());
            for (const auto& de : events) model->appendBack(de);
            if (callback) callback(true);
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomStore::loadMembers(const std::string& roomId, QPointer<QWidget> guard,
                              std::function<void(std::vector<MemberInfo>)> callback) {
    if (!client_ || !client_->isLoggedIn()) return;
    MatrixClient* c = client_;
    std::thread([guard, c, roomId, callback]() {
        auto r = c->getRoomMembers(roomId);
        std::vector<MemberInfo> members;
        if (r.ok) {
            simdjson::dom::parser parser;
            auto root = parser.parse(r.data);
            if (root.error() == simdjson::SUCCESS) {
                auto chunk = root.value()["chunk"].get_array();
                if (chunk.error() == simdjson::SUCCESS) {
                    for (auto evt : chunk.value()) {
                        auto content = evt["content"];
                        if (content.error() != simdjson::SUCCESS) continue;
                        auto ms = content.value()["membership"].get_string();
                        if (ms.error() != simdjson::SUCCESS || std::string(ms.value()) != "join") continue;
                        MemberInfo m;
                        m.membership = std::string(ms.value());
                        auto sk = evt["state_key"].get_string();
                        if (sk.error() == simdjson::SUCCESS) m.userId = std::string(sk.value());
                        auto dn = content.value()["displayname"].get_string();
                        if (dn.error() == simdjson::SUCCESS) m.displayName = std::string(dn.value());
                        auto av = content.value()["avatar_url"].get_string();
                        if (av.error() == simdjson::SUCCESS) m.avatarUrl = std::string(av.value());
                        members.push_back(std::move(m));
                    }
                }
            }
        }
        QMetaObject::invokeMethod(guard, [guard, members = std::move(members), callback]() {
            if (guard.isNull()) return;
            if (callback) callback(members);
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomStore::batchLoadRoomStates(RoomListModel* model, QPointer<QWidget> guard) {
    if (!client_ || !client_->isLoggedIn() || batchInProgress_) return;
    batchInProgress_ = true;
    std::vector<std::string> roomIds;
    for (int i = 0; i < model->rowCount(); ++i) {
        auto* rd = model->at(i);
        if (rd && !rd->isInvite && (rd->name == rd->roomId || rd->avatarUrl.empty()))
            roomIds.push_back(rd->roomId);
    }
    if (roomIds.empty()) { batchInProgress_ = false; return; }
    MatrixClient* c = client_;
    std::thread([guard, c, roomIds, model]() {
        for (const auto& roomId : roomIds) {
            auto nameResp = c->getRoomStateEvent(roomId, "m.room.name");
            auto avatarResp = c->getRoomStateEvent(roomId, "m.room.avatar");
            QMetaObject::invokeMethod(guard, [guard, model, roomId, nameResp, avatarResp]() {
                if (guard.isNull()) return;
                int row = model->findRowByRoomId(roomId);
                if (row < 0) return;
                auto* rd = const_cast<RoomData*>(model->at(row));
                if (!rd) return;
                bool changed = false;
                if (nameResp.ok && !nameResp.data.empty()) {
                    auto name = extractStringDec(nameResp.data, "name");
                    if (!name.empty() && rd->name == roomId) { rd->name = name; changed = true; }
                }
                if (avatarResp.ok && !avatarResp.data.empty()) {
                    auto url = extractStringDec(avatarResp.data, "url");
                    if (!url.empty() && rd->avatarUrl.empty()) { rd->avatarUrl = url; changed = true; }
                }
                if (changed) emit model->dataChanged(model->index(row), model->index(row));
            }, Qt::QueuedConnection);
        }
    }).detach();
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
        de.msgtype = msgType(de.contentJson);
        de.body = msgBody(de.contentJson);
        if (de.msgtype == "m.image" || de.msgtype == "m.video") {
            de.mxcUrl = extractStringDec(de.contentJson, "url");
            de.mimetype = extractStringDec(de.contentJson, "mimetype");
        }
        auto thRoot = threadRootId(de.contentJson);
        if (!thRoot.empty()) { de.isThreadReply = true; de.threadRootId = thRoot; }
    }
}

static void appendTimelineForRoom(const std::string& roomId,
    const std::vector<FastEvent>& events, TimelineModel* model,
    const std::unordered_map<std::string,std::string>* memberAvatars,
    const std::string& myUserId) {
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
        DisplayedEvent de;
        fastEventToDisplayed(e, de, roomId, nullptr);  // decryptor passed later
        if (memberAvatars && !de.senderId.empty()) {
            auto it = memberAvatars->find(de.senderId);
            if (it != memberAvatars->end()) de.avatarUrl = it->second;
        }
        model->appendBack(de);
        if (de.isThreadReply && !de.threadRootId.empty()) {
            int rootRow = model->findRow(de.threadRootId);
            if (rootRow >= 0) {
                auto* revt = model->at(rootRow);
                if (revt) { revt->threadReplyCount++; emit model->dataChanged(model->index(rootRow), model->index(rootRow)); }
            }
        }
    }
}

} // namespace progressive::desktop
