// src/ui/room/room_store.cpp — room operations extracted from MainWindow.
#include "room_store.hpp"
#include "room_data_loader.hpp"
#include "event_body_parser.hpp"
#include "../../core/session_store.hpp"
#include "../../core/engine/sync_applier.hpp"
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
#include <regex>
#include <simdjson.h>
#include "core/json_utils.hpp"

namespace progressive::desktop {

// Forward declaration (the helpers were moved to SyncApplier; this stays).
static void appendTimelineForRoom(const std::string& roomId,
    const std::vector<FastEvent>& events, TimelineModel* model,
    const std::unordered_map<std::string,std::string>* memberAvatars,
    const std::string& myUserId,
    Decryptor* decryptor = nullptr);

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

// ---- Sync update (worker thread part: no model access) ----

// ---- Sync update (UI thread part: model operations only) ----

void RoomStore::applyRoomSyncUpdate(RoomSyncUpdate& syncUpdate,
                                     RoomListModel* roomList,
                                     TimelineModel* currentTimeline,
                                     Decryptor* decryptor) {
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
        // Accumulate member avatars across syncs — Synapse omits the state
        // block on incremental syncs, so a fresh per-sync map would leave
        // every new message without an avatar.
        for (const auto& [uid, av] : syncUpdate.currentRoomAvatars) memberAvatars_[uid] = av;
        appendTimelineForRoom(syncUpdate.currentRoomId, syncUpdate.currentRoomEvents,
                              currentTimeline, &memberAvatars_,
                              "" /* myUserId passed earlier */,
                              decryptor);
    }
}

void RoomStore::loadHistory(const std::string& roomId, TimelineModel* model,
                              LifeToken token,
                              std::function<void(int, const std::string&)> callback,
                              Decryptor* decryptor) {
    dataLoader_->loadHistory(roomId, model, token, callback, decryptor);
}

void RoomStore::loadMembers(const std::string& roomId, LifeToken token,
                              const std::vector<std::string>& relevantIds,
                              std::function<void(std::vector<MemberInfo>)> callback) {
    dataLoader_->loadMembers(roomId, token, relevantIds, callback);
}

void RoomStore::batchLoadRoomStates(RoomListModel* model, LifeToken token) {
    dataLoader_->batchLoadRoomStates(model, token);
}

static DisplayedEvent makeSystemEvent(const FastEvent& e) {
    DisplayedEvent sys;
    sys.type = "progressive.system";
    sys.eventId = std::string(e.eventId);
    sys.originServerTs = e.originServerTs;
    sys.senderName = "system";
    std::string stateKey(e.stateKey.data(), e.stateKey.size());
    sys.body = SyncApplier::makeSystemBody(std::string(e.type), std::string(e.contentJson), stateKey);
    return sys;
}

static void appendTimelineForRoom(const std::string& roomId,
    const std::vector<FastEvent>& events, TimelineModel* model,
    const std::unordered_map<std::string,std::string>* memberAvatars,
    const std::string& myUserId,
    Decryptor* decryptor) {
    std::vector<DisplayedEvent> batch;
    struct PendingReaction { std::string targetId; std::string emoji; std::string sender; };
    std::vector<PendingReaction> pendingReactions;
    for (const auto& e : events) {
        if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto ms = SyncApplier::extractString(e.contentJson, "membership");
            if (ms == "join" && std::string(e.stateKey) == myUserId) continue;
        }
        if (e.type == "m.room.redaction" && !e.contentJson.empty()) {
            auto rid = SyncApplier::extractStringDec(e.contentJson, "redacts");
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
                    pendingReactions.push_back({std::string(te.value()), std::string(key.value()), std::string(e.senderId)});
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
        SyncApplier::fastEventToDisplayed(e, de, roomId, decryptor);
        if (de.type == "m.reaction" && !de.contentJson.empty()) {
            // Encrypted reactions decrypt to the m.reaction type — extract
            // them too so they never render as rows or count as replies.
            simdjson::dom::parser rp2;
            auto doc2 = rp2.parse(de.contentJson);
            if (doc2.error() == simdjson::SUCCESS) {
                auto rel2 = doc2.value()["m.relates_to"];
                auto te2 = rel2["event_id"].get_string();
                auto key2 = rel2["key"].get_string();
                if (te2.error() == simdjson::SUCCESS && key2.error() == simdjson::SUCCESS)
                    pendingReactions.push_back({std::string(te2.value()), std::string(key2.value()), de.senderId});
            }
            continue;
        }
        if (memberAvatars && !de.senderId.empty()) {
            auto it = memberAvatars->find(de.senderId);
            if (it != memberAvatars->end()) de.avatarUrl = it->second;
        }
        batch.push_back(std::move(de));
    }
    model->appendBackBatch(batch);
    for (const auto& pr : pendingReactions)
        model->addReaction(pr.targetId, pr.emoji, pr.sender);
}

} // namespace progressive::desktop
