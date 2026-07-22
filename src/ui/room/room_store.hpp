// src/ui/room/room_store.hpp — room operations extracted from MainWindow.
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <QPointer>
#include <QString>
#include <QWidget>

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

class RoomStore {
public:
    RoomStore(MatrixClient* client, SessionStore* store);

    void rebuildFromSync(const FastSyncResponse& resp,
                         RoomListModel* roomList,
                         TimelineModel* currentTimeline,
                         const std::string& currentRoomId,
                         const std::string& myUserId,
                         QString& inviteText_out,
                         bool& hasInvites_out);

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
