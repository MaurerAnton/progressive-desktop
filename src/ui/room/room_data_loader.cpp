// src/ui/room/room_data_loader.cpp — async room data loading.
#include "room_data_loader.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/thread_pool.hpp"
#include "../room/room_store.hpp"
#include "../timeline/timeline_model.hpp"
#include "../room_list_model.hpp"
#include "../profile/room_members_dialog.hpp"
#include "core/debug_log.hpp"

#include <QMetaObject>
#include <simdjson.h>

namespace progressive::desktop {

RoomDataLoader::RoomDataLoader(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store, QObject* parent)
    : QObject(parent), client_(std::move(client)), store_(std::move(store)) {}

void RoomDataLoader::loadHistory(const std::string& roomId, TimelineModel* model,
                                   LifeToken token,
                                   std::function<void(int, const std::string&)> callback) {
    if (!client_ || !client_->isLoggedIn()) { if (callback) callback(0, ""); return; }
    auto c = client_;
    QPointer<RoomDataLoader> selfGuard(this);
    ThreadPool::instance().enqueue([selfGuard, c, roomId, model, callback, token]() {
        auto result = c->getMessages(roomId, "", 30);
        QMetaObject::invokeMethod(selfGuard, [selfGuard, result, model, callback, token]() {
            if (selfGuard.isNull() || !token || !*token) return;
            if (!result.ok) { if (callback) callback(0, ""); return; }
            simdjson::dom::parser parser;
            auto root = parser.parse(result.data);
            if (root.error() != simdjson::SUCCESS) { if (callback) callback(0, ""); return; }
            auto chunk = root.value()["chunk"].get_array();
            if (chunk.error() != simdjson::SUCCESS) { if (callback) callback(0, ""); return; }
            std::vector<DisplayedEvent> events;
            std::unordered_map<std::string, std::string> localAvatars;
            std::vector<std::tuple<std::string, std::string, std::string>> pendingReactions;
            for (auto evt : chunk.value()) {
                DisplayedEvent de;
                auto t = evt["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());
                if (de.type == "m.reaction")
                    LOG(LogChannel::DBG, "loadHistory: reaction contentJson=[%.200s]",
                        de.contentJson.c_str());
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
                if (de.type == "m.room.member" && !de.contentJson.empty()) {
                    auto av = extractStringDec(de.contentJson, "avatar_url");
                    std::string stateKey;
                    auto sk = evt["state_key"].get_string();
                    if (sk.error() == simdjson::SUCCESS) stateKey = std::string(sk.value());
                    if (!av.empty() && !stateKey.empty()) localAvatars[stateKey] = av;
                }
                if (de.type == "m.room.redaction" && !de.contentJson.empty()) {
                    auto rid = extractStringDec(de.contentJson, "redacts");
                    if (!rid.empty()) model->markDeleted(rid);
                    continue;
                }
                if (de.type == "m.room.member" || de.type == "m.room.topic" ||
                    de.type == "m.room.name" || de.type == "m.room.encryption" ||
                    de.type == "m.room.create" || de.type == "m.room.avatar") {
                    std::string stateKey;
                    auto sk = evt["state_key"].get_string();
                    if (sk.error() == simdjson::SUCCESS) stateKey = std::string(sk.value());
                    auto body = makeSystemBody(de.type, de.contentJson, stateKey);
                    if (!body.empty()) {
                        DisplayedEvent sys;
                        sys.type = "progressive.system";
                        sys.eventId = de.eventId;
                        sys.senderName = "system";
                        sys.originServerTs = de.originServerTs;
                        sys.body = body;
                        events.push_back(std::move(sys));
                    }
                    continue;
                }
                if (de.type == "m.reaction" && !de.contentJson.empty()) {
                    std::string eid, emoji;
                    std::string pid = "\"event_id\":";
                    auto rp = de.contentJson.find(pid);
                    if (rp == std::string::npos) { pid = "\"event_id\": "; rp = de.contentJson.find(pid); }
                    if (rp != std::string::npos) {
                        auto es = de.contentJson.find('"', rp + (int)pid.size());
                        auto ee = de.contentJson.find('"', es + 1);
                        if (es != std::string::npos && ee != std::string::npos)
                            eid = de.contentJson.substr(es + 1, ee - es - 1);
                    }
                    std::string pk = "\"key\":";
                    auto kp = de.contentJson.find(pk);
                    if (kp == std::string::npos) { pk = "\"key\": "; kp = de.contentJson.find(pk); }
                    if (kp != std::string::npos) {
                        auto ks = de.contentJson.find('"', kp + (int)pk.size());
                        auto ke = de.contentJson.find('"', ks + 1);
                        if (ks != std::string::npos && ke != std::string::npos)
                            emoji = de.contentJson.substr(ks + 1, ke - ks - 1);
                    }
                    LOG(LogChannel::DBG, "loadHistory: parsed reaction eid=%s emoji=%s sender=%s",
                        eid.c_str(), emoji.c_str(), de.senderId.c_str());
                    if (!eid.empty() && !emoji.empty()) {
                        pendingReactions.emplace_back(eid, emoji, de.senderId);
                    }
                    continue;
                }
                if (de.type == "m.typing" || de.type == "m.receipt" ||
                    de.type == "m.fully_read") {
                    continue;
                }
                if (de.type != "m.room.message" && de.type != "m.room.encrypted") continue;
                if (de.type == "m.room.message") {
                    de.msgtype = msgType(de.contentJson);
                    de.body = msgBody(de.contentJson);
                }
                events.push_back(std::move(de));
            }
            std::reverse(events.begin(), events.end());
            for (auto& evt : events) {
                if (evt.avatarUrl.empty() && !evt.senderId.empty()) {
                    auto it = localAvatars.find(evt.senderId);
                    if (it != localAvatars.end()) evt.avatarUrl = it->second;
                }
            }
            LOG(LogChannel::DBG, "loadHistory: appended %zu events, %zu pending reactions",
                events.size(), pendingReactions.size());
            model->appendBackBatch(events);
            for (const auto& [eid, emoji, senderId] : pendingReactions) {
                model->addReaction(eid, emoji, senderId);
                int row = model->findRow(eid);
                LOG(LogChannel::DBG, "loadHistory: addReaction eid=%s emoji=%s foundRow=%d",
                    eid.c_str(), emoji.c_str(), row);
            }
            int count = (int)events.size();
            std::string pBatch;
            auto et = root.value()["end"].get_string();
            if (et.error() == simdjson::SUCCESS && et.value().size() > 0)
                pBatch = std::string(et.value());
            else { auto st = root.value()["start"].get_string();
                if (st.error() == simdjson::SUCCESS) pBatch = std::string(st.value()); }
            if (callback) callback(count, pBatch);
        }, Qt::QueuedConnection);
    });
}

void RoomDataLoader::loadMembers(const std::string& roomId, LifeToken token,
                                   const std::vector<std::string>& relevantIds,
                                   std::function<void(std::vector<MemberInfo>)> callback) {
    if (!client_ || !client_->isLoggedIn()) return;
    auto c = client_;
    QPointer<RoomDataLoader> selfGuard(this);
    ThreadPool::instance().enqueue([selfGuard, c, roomId, relevantIds, callback, token]() {
        auto r = c->getRoomMembers(roomId);
        std::vector<MemberInfo> members;
        bool gotMembers = false;
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
                    gotMembers = true;
                }
            }
        }
        if (!gotMembers && !relevantIds.empty()) {
            for (const auto& uid : relevantIds) {
                auto pr = c->getUserProfile(uid);
                if (pr.ok && !pr.data.empty()) {
                    MemberInfo m;
                    m.userId = uid;
                    m.membership = "join";
                    simdjson::dom::parser pp;
                    auto doc = pp.parse(pr.data);
                    if (doc.error() == simdjson::SUCCESS) {
                        auto av = doc.value()["avatar_url"].get_string();
                        if (av.error() == simdjson::SUCCESS) m.avatarUrl = std::string(av.value());
                        auto dn = doc.value()["displayname"].get_string();
                        if (dn.error() == simdjson::SUCCESS) m.displayName = std::string(dn.value());
                    }
                    members.push_back(m);
                }
            }
        }
        QMetaObject::invokeMethod(selfGuard, [selfGuard, members = std::move(members), callback, token]() {
            if (selfGuard.isNull() || !token || !*token) return;
            if (callback) callback(members);
        }, Qt::QueuedConnection);
    });
}

void RoomDataLoader::batchLoadRoomStates(RoomListModel* model, LifeToken token) {
    if (!client_ || !client_->isLoggedIn()) return;
    std::vector<std::string> roomIds;
    for (int i = 0; i < model->rowCount(); ++i) {
        auto* rd = model->at(i);
        if (rd && !rd->isInvite && (rd->name == rd->roomId || rd->avatarUrl.empty()))
            roomIds.push_back(rd->roomId);
    }
    if (roomIds.empty()) return;
    auto c = client_;
    QPointer<RoomDataLoader> selfGuard(this);
    ThreadPool::instance().enqueue([selfGuard, c, roomIds, model, token]() {
        for (const auto& roomId : roomIds) {
            auto nameResp = c->getRoomStateEvent(roomId, "m.room.name");
            auto avatarResp = c->getRoomStateEvent(roomId, "m.room.avatar");
            QMetaObject::invokeMethod(selfGuard, [selfGuard, model, roomId, nameResp, avatarResp, token]() {
                if (selfGuard.isNull() || !token || !*token) return;
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
    });
}

} // namespace progressive::desktop
