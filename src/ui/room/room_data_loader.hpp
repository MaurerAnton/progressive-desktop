// src/ui/room/room_data_loader.hpp — async room data loading.
#pragma once
#include <QObject>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace progressive::desktop {

using LifeToken = std::shared_ptr<bool>;

class MatrixClient;
class SessionStore;
class RoomListModel;
class TimelineModel;
struct MemberInfo;

class RoomDataLoader : public QObject {
    Q_OBJECT
public:
    RoomDataLoader(MatrixClient* client, SessionStore* store,
                   QObject* parent = nullptr);

    void setClient(MatrixClient* c) { client_ = c; }
    void setSessionStore(SessionStore* s) { store_ = s; }

    void loadHistory(const std::string& roomId, TimelineModel* model,
                     LifeToken token, std::function<void(int, const std::string&)> callback);

    void loadMembers(const std::string& roomId, LifeToken token,
                     const std::vector<std::string>& userIds,
                     std::function<void(std::vector<MemberInfo>)> callback);

    void batchLoadRoomStates(RoomListModel* model, LifeToken token);

private:
    MatrixClient* client_;
    SessionStore* store_;
};

} // namespace progressive::desktop
