// src/ui/room/room_store.hpp — room operations extracted from MainWindow.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <QPointer>
#include <QString>
#include <QWidget>
#include "../room_list_model.hpp"

namespace progressive::desktop {

using LifeToken = std::shared_ptr<bool>;

class Decryptor;
class MatrixClient;
class SessionStore;
class RoomListModel;
class TimelineModel;
class RoomDataLoader;
struct FastSyncResponse;
struct FastRoom;
struct FastEvent;
struct MemberInfo;

// RoomMeta + RoomSyncUpdate moved to src/core/engine/sync_applier.hpp (X1).
struct RoomSyncUpdate;

class RoomStore {
public:
    RoomStore(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store);

    void setClient(std::shared_ptr<MatrixClient> c);
    void setSessionStore(std::shared_ptr<SessionStore> s);

    RoomDataLoader* dataLoader() const { return dataLoader_.get(); }

    // Heavy part moved to SyncApplier::prepareRoomSyncUpdate (core/engine).
    // Light part — runs on UI thread, only model operations
    void applyRoomSyncUpdate(RoomSyncUpdate& syncUpdate,
                              RoomListModel* roomList,
                              TimelineModel* currentTimeline,
                              Decryptor* decryptor = nullptr);

    void loadHistory(const std::string& roomId,
                     TimelineModel* model,
                     LifeToken token,
                     std::function<void(int eventCount,
                                         const std::string& prevBatch)> callback,
                     Decryptor* decryptor = nullptr);

    void loadMembers(const std::string& roomId,
                     LifeToken token,
                     const std::vector<std::string>& relevantIds,
                     std::function<void(std::vector<MemberInfo>)> callback);

    void batchLoadRoomStates(RoomListModel* model,
                              LifeToken token);

    // Apply re-decrypted E2EE events to the timeline (drains from decryptor).
private:
    std::shared_ptr<MatrixClient> client_;
    std::shared_ptr<SessionStore> store_;
    std::unique_ptr<RoomDataLoader> dataLoader_;
    // Accumulated member-avatar map for the current room (the per-sync map
    // would be empty on incremental syncs — Synapse omits the state block).
    std::unordered_map<std::string, std::string> memberAvatars_;
    std::string lastAppliedRoom_;
    bool batchInProgress_ = false;
};

std::string extractStringDec(std::string_view json, const std::string& key);
std::string makeSystemBody(const std::string& type, const std::string& contentJson,
                            const std::string& stateKey);
std::string msgType(std::string_view json);
std::string msgBody(std::string_view json);
std::string extractThreadRootId(std::string_view contentJson);
std::string extractReplyToId(std::string_view contentJson);

} // namespace progressive::desktop
