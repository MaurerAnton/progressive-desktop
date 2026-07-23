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

class MatrixClient;
class SessionStore;
class RoomListModel;
class TimelineModel;
class RoomDataLoader;
struct FastSyncResponse;
struct FastRoom;
struct FastEvent;
struct MemberInfo;

struct RoomMeta {
    std::string name;
    std::string avatarUrl;
    std::string dmDisplayName;
    std::string dmAvatarUrl;
    std::string canonicalAlias;
    bool isEncrypted = false;
};

// Prepared update — computed on worker thread, applied on UI thread
struct RoomSyncUpdate {
    std::vector<RoomData> roomsToUpsert;
    std::vector<std::string> roomsToRemove;
    std::vector<RoomData> invitedRooms;
    QString inviteText;
    int inviteCount = 0;
    bool currentRoomUpdated = false;
    std::string currentRoomId;
    std::vector<FastEvent> currentRoomEvents;
    std::unordered_map<std::string,std::string> currentRoomAvatars;
    std::string lastNotificationBody;  // last highlight message body for notifications
};

class RoomStore {
public:
    RoomStore(MatrixClient* client, SessionStore* store);

    void setClient(MatrixClient* c);
    void setSessionStore(SessionStore* s);

    RoomDataLoader* dataLoader() const { return dataLoader_.get(); }

    // Heavy part — runs on worker thread, no model access
    static RoomSyncUpdate prepareRoomSyncUpdate(const FastSyncResponse& resp,
                                                 const std::string& currentRoomId,
                                                 const std::string& myUserId);

    // Light part — runs on UI thread, only model operations
    void applyRoomSyncUpdate(RoomSyncUpdate& syncUpdate,
                              RoomListModel* roomList,
                              TimelineModel* currentTimeline);

    void loadHistory(const std::string& roomId,
                     TimelineModel* model,
                     LifeToken token,
                     std::function<void(int eventCount,
                                         const std::string& prevBatch)> callback);

    void loadMembers(const std::string& roomId,
                     LifeToken token,
                     const std::vector<std::string>& relevantIds,
                     std::function<void(std::vector<MemberInfo>)> callback);

    void batchLoadRoomStates(RoomListModel* model,
                              LifeToken token);

    static RoomMeta extractRoomMeta(const FastRoom& room,
                                     const std::string& myUserId);
    static std::string extractLastMessageBody(
        const std::vector<FastEvent>& events);

private:
    MatrixClient* client_;
    SessionStore* store_;
    std::unique_ptr<RoomDataLoader> dataLoader_;
    bool batchInProgress_ = false;
};

std::string extractStringDec(std::string_view json, const std::string& key);
std::string makeSystemBody(const std::string& type, const std::string& contentJson,
                            const std::string& stateKey);
std::string msgType(std::string_view json);
std::string msgBody(std::string_view json);

} // namespace progressive::desktop
