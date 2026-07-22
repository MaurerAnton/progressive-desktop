// src/ui/room/room_store.hpp — room operations extracted from MainWindow.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <QPointer>
#include <QString>
#include <QWidget>
#include "../room_list_model.hpp"

namespace progressive::desktop {

class MatrixClient;
class SessionStore;
class RoomListModel;
class TimelineModel;
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
};

class RoomStore {
public:
    RoomStore(MatrixClient* client, SessionStore* store);

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
                     QPointer<QWidget> guard,
                     std::function<void(int eventCount,
                                        const std::string& prevBatch)> callback);

    void loadMembers(const std::string& roomId,
                     QPointer<QWidget> guard,
                     const std::vector<std::string>& relevantIds,
                     std::function<void(std::vector<MemberInfo>)> callback);

    void batchLoadRoomStates(RoomListModel* model,
                             QPointer<QWidget> guard);

    static RoomMeta extractRoomMeta(const FastRoom& room,
                                     const std::string& myUserId);
    static std::string extractLastMessageBody(
        const std::vector<FastEvent>& events);

private:
    MatrixClient* client_;
    SessionStore* store_;
    bool batchInProgress_ = false;
};

} // namespace progressive::desktop
